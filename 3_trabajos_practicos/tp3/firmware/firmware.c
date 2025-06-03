#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "lcd.h"
#include "helper.h"

// I2C definiciones
#define I2C_PORT i2c0
#define I2C_SDA 4
#define I2C_SCL 5
#define LCD_ADDR 0x27

#define MAX_COUNTING 4096
#define GPIO_PULSE 15
#define GPIO_PWM 12

#define SAMPLE_MS 250

SemaphoreHandle_t semaphore_count;


/**
 * @brief Tarea de conteo de pulsos
 */
void task_pulse_count(void *params) {
    while(1) {
        if (gpio_get(GPIO_PULSE)) {
            xSemaphoreGive(semaphore_count);
            // Espera a que el pulso se baje
            while (gpio_get(GPIO_PULSE));
        }
    }
}



/**
 * @brief Tarea de escritura en el LCD
 */
void task_lcd_write(void *params) {
    char msg[MAX_CHARS] = {0};
    
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));


        uint32_t pulse_count = uxSemaphoreGetCount(semaphore_count);
        float freq_value = pulse_count * (1000 / (float) SAMPLE_MS);
        if (pulse_count == MAX_COUNTING)
            snprintf(msg, MAX_CHARS, "Max f: %.1fHz", freq_value);
        else
            snprintf(msg, MAX_CHARS, "%.2fHz", freq_value);
        
        lcd_clear();
        lcd_set_cursor(0,0);
        lcd_string("Frecuencia:");
        lcd_set_cursor(1,0);
        lcd_string(msg);
        
        xQueueReset(semaphore_count);
        printf("Frecuencia: %s\n", msg);
        printf("Pulsos contados: %d\n", pulse_count);
    }
}


int main()
{
    stdio_init_all();
    pwm_user_init(GPIO_PWM, 5000);

    gpio_init(GPIO_PULSE);
    gpio_set_dir(GPIO_PULSE, GPIO_IN);
    gpio_pull_down(GPIO_PULSE);


    // Inicializacion del I2C. Freq 400Khz.
    i2c_init(I2C_PORT, 400*1000);
    
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Inicializacion del LCD
    lcd_init(I2C_PORT, LCD_ADDR);

    // Inicializacion del semaforo
    semaphore_count = xSemaphoreCreateCounting(MAX_COUNTING, 0);
    if (semaphore_count == NULL) {
        printf("Error al crear el semáforo\n");
        return -1;
    }

    // Inicializacion de tareas
    xTaskCreate(task_lcd_write,
        "task_lcd_write",
        configMINIMAL_STACK_SIZE * 2, 
        NULL,
        tskIDLE_PRIORITY + 2,
        NULL
    );
    xTaskCreate(task_pulse_count,
        "task_pulse_count",
        configMINIMAL_STACK_SIZE * 2, 
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL
    );

    // Arranca el scheduler
    vTaskStartScheduler();
    while (true);
}
