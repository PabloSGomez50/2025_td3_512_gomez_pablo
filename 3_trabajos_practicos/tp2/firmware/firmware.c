#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/irq.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// Definicion de constantes
#define ADC_CHANNEL 4
#define ADC_SAMPLES 2
#define ADC_RESOLUTION ((1 << 12) - 1)
#define ADC_VREF 3.3f
#define ADC_PERIOD_MS 1000

QueueHandle_t temp_queue;

float convert_adc_to_celsius(uint16_t adc_value) {
    float adc_voltage = (float)(adc_value * ADC_VREF) / ADC_RESOLUTION;
    return 27 - (adc_voltage - 0.706) / 0.001721;
}

/**
 * @brief Handler para la interrupcion del ADC
 */
void adc_irq_handler(void) {
    // Variable para verificar la necesidad de un cambio tarea
    BaseType_t to_higher_priority_task = pdFALSE;
    // Deshabilito la interrupcion y detengo el ADC
    adc_irq_set_enabled(false);
    adc_run(false);
    // El calculo de temperatura sale de la documentacion del SDK
    uint16_t data = adc_fifo_get();
    adc_fifo_drain();
    // Envio por cola
    xQueueSendFromISR(temp_queue, &data, &to_higher_priority_task);
    // Reviso si es necesario el cambio a otra tarea
    portYIELD_FROM_ISR(to_higher_priority_task);
}

/**
 * @brief Tarea de lectura de datos
 */
void task_adc_enabled(void *params) {

    while(1) {
        adc_irq_set_enabled(true);
        adc_run(true);
        // Espero a la siguiente lectura
        vTaskDelay(pdMS_TO_TICKS(ADC_PERIOD_MS)); 
    }
}
/**
 * @brief Tarea de envio de datos
 */
void task_serial_monitor(void *params) {
    
    uint16_t adc_value;
    while(1) {
        xQueueReceive(temp_queue, &adc_value, portMAX_DELAY);
        printf("Temperatura: %.2f °C\n", convert_adc_to_celsius(adc_value));
    }
}

/**
 * @brief Programa principal
 */
int main(void) {
    stdio_init_all();
    // Inicializacion del adc temperatura
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(ADC_CHANNEL);

    // configuro la interrupcion y fifo
    adc_fifo_setup(true, false, 1, false, false);
    adc_irq_set_enabled(true);
    irq_set_exclusive_handler(ADC_IRQ_FIFO, adc_irq_handler);
    irq_set_enabled(ADC_IRQ_FIFO, true);
    
    temp_queue = xQueueCreate(ADC_SAMPLES, sizeof(uint16_t));
    if (temp_queue == NULL) {
        printf("Error al crear la cola de temperatura\n");
        return -1;
    }

    // Inicializacion de tareas
    xTaskCreate(task_adc_enabled,
        "task_adc_enabled",
        configMINIMAL_STACK_SIZE, 
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL
    );
    xTaskCreate(task_serial_monitor,
        "task_serial_monitor",
        configMINIMAL_STACK_SIZE * 2, 
        NULL,
        tskIDLE_PRIORITY + 2,
        NULL
    );

    // Arranca el scheduler
    vTaskStartScheduler();
    while(1);
}