#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "lcd.h"
#include "bmp280.h"

// I2C definiciones
#define I2C_PORT i2c0
#define I2C_SDA 4
#define I2C_SCL 5
#define LCD_ADDR 0x27

#define SLEEP_TIME_LCD 150 // Tiempo de espera en ms para la LCD

// Mutex para sincronizacion de tareas
SemaphoreHandle_t mutex_i2c;
QueueHandle_t queue_bmp280;

struct bmp280_data {
    float temperature;
    int32_t pressure;
};

void task_read_bmp280(void *pvParameters) {
    struct bmp280_calib_param bmp280_calib = {0};
    bmp280_get_calib_params(&bmp280_calib);
    uint32_t raw_temp, raw_pressure;
    struct bmp280_data data_aux;

    while(1) {
        xSemaphoreTake(mutex_i2c, portMAX_DELAY);
        bmp280_read_raw(&raw_temp, &raw_pressure);
        data_aux.temperature = bmp280_convert_temp(raw_temp, &bmp280_calib);
        data_aux.pressure = bmp280_convert_pressure(raw_pressure, raw_temp, &bmp280_calib);
        xSemaphoreGive(mutex_i2c);
        
        xQueueSend(queue_bmp280, &data_aux, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void task_lcd_display(void *pvParameters) {
    struct bmp280_data data_aux;

    while(1) {

        xQueueReceive(queue_bmp280, &data_aux, portMAX_DELAY);
        xSemaphoreTake(mutex_i2c, portMAX_DELAY);
        char buffer[MAX_CHARS];
        snprintf(buffer, MAX_CHARS, "Temp: %.2f*C", data_aux.temperature);
        lcd_set_cursor(0, 0);
        lcd_string(buffer);
        
        snprintf(buffer, MAX_CHARS, "Pres: %.2fkPa", data_aux.pressure / 1000.0);
        lcd_set_cursor(1, 0);
        lcd_string(buffer);

        xSemaphoreGive(mutex_i2c);

        vTaskDelay(pdMS_TO_TICKS(SLEEP_TIME_LCD)); // Espera de 500 ms
    }
}

int main()
{
    stdio_init_all();
    // Inicializacion de Semaforo y Cola
    mutex_i2c = xSemaphoreCreateMutex();
    queue_bmp280 = xQueueCreate(3, sizeof(struct bmp280_data));
    // Inicializacion del I2C. Freq 400Khz.
    i2c_init(I2C_PORT, 400*1000);
    
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Inicializacion del LCD
    lcd_init(I2C_PORT, LCD_ADDR);

    // Inicializacion del BMP280
    bmp280_init(I2C_PORT);

    // Creacion de tareas
    xTaskCreate(task_read_bmp280, "task_read_bmp280", configMINIMAL_STACK_SIZE * 1, NULL, 1, NULL);
    xTaskCreate(task_lcd_display, "task_lcd_display", configMINIMAL_STACK_SIZE * 1, NULL, 1, NULL);

    vTaskStartScheduler();
    while(true);
}
