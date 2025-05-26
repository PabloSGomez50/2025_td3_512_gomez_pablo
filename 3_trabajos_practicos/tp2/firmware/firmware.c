#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// Definicion de constantes
#define ADC_CHANNEL 4
#define ADC_SAMPLES 8
#define ADC_RESOLUTION (1 << 12)
#define ADC_VREF 3.3f

QueueHandle_t temp_queue;

float convert_adc_to_celsius(uint16_t adc_value) {
    float adc_voltage = (float)(adc_value * ADC_VREF) / ADC_RESOLUTION;
    return 27 - (adc_voltage - 0.706) / 0.001721;
}

/**
 * @brief Tarea de lectura de datos
 */
void task_adc_temp_queue(void *params) {

    while(1) {
        uint16_t aux = adc_read();
        float temp_celsius = convert_adc_to_celsius(aux);
        xQueueSend(temp_queue, &temp_celsius, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}
/**
 * @brief Tarea de envio de datos
 */
void task_serial_monitor(void *params) {

    while(1) {
        float temp_celsius;
        xQueueReceive(temp_queue, &temp_celsius, portMAX_DELAY);
        printf("Temperatura: %.2f °C\n", temp_celsius);
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
    
    temp_queue = xQueueCreate(ADC_SAMPLES, sizeof(float));
    if (temp_queue == NULL) {
        printf("Error al crear la cola de temperatura\n");
        return -1;
    }

    // Inicializacion de tareas
    xTaskCreate(task_adc_temp_queue,
        "task_adc_temp_queue",
        configMINIMAL_STACK_SIZE * 2, 
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL
    );
    xTaskCreate(task_serial_monitor,
        "task_serial_monitor",
        configMINIMAL_STACK_SIZE * 2, 
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL
    );

    // Arranca el scheduler
    vTaskStartScheduler();
    while(1);
}