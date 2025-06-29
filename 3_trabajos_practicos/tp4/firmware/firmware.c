#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "lcd.h"
#include "bmp280.h"

// I2C definiciones
#define I2C_PORT i2c0
#define I2C_SDA 16
#define I2C_SCL 17
#define LCD_ADDR 0x27

#define SLEEP_TIME_LCD 400 // Tiempo de espera en ms para la LCD

#define BTN_GPIO 14
#define LED_GPIO 15
#define DEBOUNCE_TIME 70 // Tiempo de debounce en ms
#define MAX_MENU_NUM 2

#define TEMP_SETPOINT 20.0f
#define Kp 50
#define CONTROLLER_REFRESH_MS 100


// Mutex para sincronizacion de tareas
SemaphoreHandle_t mutex_i2c;
SemaphoreHandle_t bin_btn;
QueueHandle_t queue_bmp280;

uint8_t menu_num = 0;

struct bmp280_data {
    float temperature;
    int32_t pressure;
};

void menu_nro1(struct bmp280_data data);
void menu_nro2(struct bmp280_data data);
void setup_pwm(uint8_t gpio);

void btn_irq_handler(uint gpio, uint32_t events) {
    static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(bin_btn, &xHigherPriorityTaskWoken);
    gpio_set_irq_enabled(gpio, events, false);

}

void task_btn_handler(void *pvParameters) {
    uint8_t btn_gpio = *(uint8_t *)pvParameters;
    gpio_init(btn_gpio);
    gpio_set_dir(btn_gpio, GPIO_IN);
    gpio_set_irq_enabled_with_callback(btn_gpio, GPIO_IRQ_EDGE_RISE, true, &btn_irq_handler);
    gpio_set_irq_enabled_with_callback(btn_gpio, GPIO_IRQ_EDGE_FALL, false, &btn_irq_handler);

    while(1) {
        xSemaphoreTake(bin_btn, portMAX_DELAY);
        menu_num = (menu_num + 1) % MAX_MENU_NUM;
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME)); // Espera para evitar rebotes
        if (gpio_get(btn_gpio)) {
            gpio_set_irq_enabled(btn_gpio, GPIO_IRQ_EDGE_FALL, true);
            xSemaphoreTake(bin_btn, portMAX_DELAY); // Asegura que no se procese el evento de liberación del botón
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME)); // Espera para evitar rebotes
        }
        gpio_set_irq_enabled(btn_gpio, GPIO_IRQ_EDGE_RISE, true);
    }
}


void task_read_bmp280(void *pvParameters) {
    // Inicializacion del BMP280
    bmp280_init(I2C_PORT);
    
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

void task_led_error(void *pvParameters) {

    // Configuración del PWM
    setup_pwm(LED_GPIO);
    struct bmp280_data data_aux;
    while(1) {
        xQueuePeek(queue_bmp280, &data_aux, portMAX_DELAY);
        int16_t duty = (int16_t) ((data_aux.temperature - TEMP_SETPOINT) * Kp);

        if (duty < 0) {
            duty *= -1;
        }
        if (duty > 1023) {
            duty = 1023; // Limita el duty cycle al máximo
        }
        pwm_set_gpio_level(LED_GPIO, duty);
        vTaskDelay(pdMS_TO_TICKS(CONTROLLER_REFRESH_MS)); // Espera para evitar saturar el PWM
    }
}

void task_lcd_display(void *pvParameters) {
    // Inicializacion del LCD
    lcd_init(I2C_PORT, LCD_ADDR);

    struct bmp280_data data_aux;
    while(1) {

        xQueueReceive(queue_bmp280, &data_aux, portMAX_DELAY);
        xSemaphoreTake(mutex_i2c, portMAX_DELAY);
        lcd_clear();
        switch(menu_num) {
            case 0:
                menu_nro1(data_aux);
                break;
            case 1:
                menu_nro2(data_aux);
                break;
        }

        xSemaphoreGive(mutex_i2c);

        vTaskDelay(pdMS_TO_TICKS(SLEEP_TIME_LCD));
    }
}

int main()
{
    stdio_init_all();
    // Inicializacion de Semaforo y Cola
    mutex_i2c = xSemaphoreCreateMutex();
    bin_btn = xSemaphoreCreateBinary();
    queue_bmp280 = xQueueCreate(3, sizeof(struct bmp280_data));
    uint8_t btn_gpio = BTN_GPIO;

    // Inicializacion del I2C. Freq 400Khz.
    i2c_init(I2C_PORT, 400*1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Creacion de tareas
    xTaskCreate(task_led_error, "task_led_error", configMINIMAL_STACK_SIZE * 1, NULL, 2, NULL);
    xTaskCreate(task_btn_handler, "task_btn_handler", configMINIMAL_STACK_SIZE * 1, &btn_gpio, 2, NULL);
    xTaskCreate(task_read_bmp280, "task_read_bmp280", configMINIMAL_STACK_SIZE * 1, NULL, 1, NULL);
    xTaskCreate(task_lcd_display, "task_lcd_display", configMINIMAL_STACK_SIZE * 1, NULL, 1, NULL);

    vTaskStartScheduler();
    while(true);
}


void menu_nro1(struct bmp280_data data) {
    char buffer[MAX_CHARS];
    snprintf(buffer, MAX_CHARS, "Temp: %.2f%cC", data.temperature, (char) 223);
    lcd_set_cursor(0, 0);
    lcd_string(buffer);
    
    snprintf(buffer, MAX_CHARS, "Pres: %.2fkPa", data.pressure / 1000.0);
    lcd_set_cursor(1, 0);
    lcd_string(buffer);
}

void menu_nro2(struct bmp280_data data) {
    char buffer[MAX_CHARS];
    snprintf(buffer, MAX_CHARS, "Error: %.2f%cC", TEMP_SETPOINT - data.temperature, (char) 223);
    lcd_set_cursor(0, 0);
    lcd_string(buffer);
    
    snprintf(buffer, MAX_CHARS, "SetP: %.2f%cC", TEMP_SETPOINT, (char) 223);
    lcd_set_cursor(1, 0);
    lcd_string(buffer);
}

void setup_pwm(uint8_t gpio) {
    // Asigna función de PWM
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    // Configura frecuencia de PWM e inicializa
    uint32_t slice = pwm_gpio_to_slice_num(gpio);
    pwm_set_clkdiv(slice, 140);
    pwm_set_wrap(slice, 1024);
    pwm_set_gpio_level(gpio, 0);
    pwm_set_enabled(slice, true);

}