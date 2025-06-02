#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lcd.h"

#include "semphr.h"

// I2C definiciones
#define I2C_PORT i2c0
#define I2C_SDA 4
#define I2C_SCL 5
#define LCD_ADDR 0x27

#define FREQ_MAX 10000
#define MAX_COUNTING 1024
#define GPIO_PULSE 15

SemaphoreHandle_t semaphore_count;

/**
 * @brief Tarea de conteo de pulsos
 */
void task_pulse_count(void *params) {
    bool pulse_detected = false;
    uint32_t count = 0;

    while (1) {
        if (gpio_get(GPIO_PULSE)) {
            if (!pulse_detected) {
                pulse_detected = true;
                count++;
                if (count >= MAX_COUNTING) {
                    printf("Max count reached!\n");
                    count = 0; // Reset count
                }
            }
        } else {
            pulse_detected = false;
        }
    }
}


void task_pulse_count_mutex(void *params) {
    bool pulse_detected = false;
    while (1) {
        if (gpio_get(GPIO_PULSE)) {
            pulse_detected = true;
            if (xSemaphoreTake(semaphore_count, 0) == pdFALSE) {
                printf("Max count reached!\n");
            }
        } else {
            pulse_detected = false;
        }
    }
}


/**
 * @brief Tarea de escritura en el LCD
 */
void task_lcd_write(void *params) {
    lcd_clear();
    lcd_string("Freq en Hz:");

    char msg[MAX_CHARS] = {0};
    uint32_t start_time = to_ms_since_boot(get_absolute_time());

    while(1) {
        uint32_t execution_time = to_ms_since_boot(get_absolute_time()) - start_time;
        snprintf(msg, MAX_CHARS, "%4umS", execution_time);
        lcd_set_cursor(1,0);
        lcd_string(msg);
        // Espero a la siguiente lectura
        vTaskDelay(pdMS_TO_TICKS(250)); 
    }
}


int main()
{
    stdio_init_all();

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
    semaphore_count = xSemaphoreCreateCounting(0, MAX_COUNTING);
    if (semaphore_count == NULL) {
        printf("Error al crear el semáforo\n");
        return -1;
    }

    // Inicializacion de tareas
    xTaskCreate(task_lcd_write,
        "task_lcd_write",
        configMINIMAL_STACK_SIZE, 
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL
    );

    // Arranca el scheduler
    vTaskStartScheduler();
    while (true);
}
