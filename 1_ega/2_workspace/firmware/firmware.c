#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "lcd.h"

// I2C definiciones
#define I2C_PORT i2c0
#define I2C_SDA 16
#define I2C_SCL 17
#define LCD_ADDR 0x27

#define SLEEP_TIME_LCD 400 // Tiempo de espera en ms para la LCD

#define BTN_MENU_GPIO 14
#define BTN_STOP_GPIO 13

#define LED_GPIO 15
#define DEBOUNCE_TIME 70 // Tiempo de debounce en ms
#define MAX_MENU_NUM 2

#define TEMP_THRESHOLD 100.0f
#define Kp 100
#define CONTROLLER_REFRESH_MS 100
#define MAX_PWM_DUTY 1024


// Mutex para sincronizacion de tareas
SemaphoreHandle_t mutex_i2c;
SemaphoreHandle_t bin_btn_1;
SemaphoreHandle_t bin_btn_2;

struct btn_data_t {
    uint8_t gpio;
    SemaphoreHandle_t *sem_bin;
    uint8_t * counter;
    uint8_t max_counter;
};


uint8_t menu_num = 0, index_num = 0;
float resistance_target = 500.0f;

void setup_pwm(uint8_t gpio);
void set_lcd_text(const char *line1, const char *line2);

void btn_irq_handler(uint gpio, uint32_t events) {
    static BaseType_t xHigherPriorityTaskWoken = pdTRUE;
    gpio_set_irq_enabled(gpio, events, false);
    if (gpio == BTN_MENU_GPIO) {
        xSemaphoreGiveFromISR(bin_btn_1, &xHigherPriorityTaskWoken);       
    }
    if (gpio == BTN_STOP_GPIO) {
        xSemaphoreGiveFromISR(bin_btn_2, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void task_btn_pull_up(void *pvParameters) {
    struct btn_data_t btn_data = *(struct btn_data_t *) pvParameters;
    gpio_init(btn_data.gpio);
    gpio_set_dir(btn_data.gpio, GPIO_IN);
    gpio_pull_up(btn_data.gpio);
    gpio_set_irq_enabled_with_callback(btn_data.gpio, GPIO_IRQ_EDGE_RISE, false, &btn_irq_handler);
    gpio_set_irq_enabled_with_callback(btn_data.gpio, GPIO_IRQ_EDGE_FALL, true, &btn_irq_handler);

    while(1) {
        xSemaphoreTake(*btn_data.sem_bin, portMAX_DELAY);
        *btn_data.counter = (*btn_data.counter + 1) % btn_data.max_counter;
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME));

        if (!gpio_get(btn_data.gpio)) {
            gpio_set_irq_enabled(btn_data.gpio, GPIO_IRQ_EDGE_RISE, true);
            xSemaphoreTake(*btn_data.sem_bin, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME));
        }
        gpio_set_irq_enabled(btn_data.gpio, GPIO_IRQ_EDGE_FALL, true);
    }
}



void task_led_error(void *pvParameters) {

    // Configuración del PWM
    setup_pwm(LED_GPIO);
    while(1) {
        int16_t duty = Kp * index_num;

        if (duty < 0) {
            duty *= -1;
        }
        if (duty > MAX_PWM_DUTY) {
            duty = MAX_PWM_DUTY; // Limita el duty cycle al máximo
        }
        pwm_set_gpio_level(LED_GPIO, duty);
        vTaskDelay(pdMS_TO_TICKS(CONTROLLER_REFRESH_MS)); // Espera para evitar saturar el PWM
    }
}

void task_lcd_display(void *pvParameters) {
    // Inicializacion del LCD
    lcd_init(I2C_PORT, LCD_ADDR);
    char line1[MAX_CHARS], line2[MAX_CHARS];
    while(1) {

        xSemaphoreTake(mutex_i2c, portMAX_DELAY);
        switch(menu_num) {
            case 0:
                set_lcd_text("Menu Principal", "Presione Boton");
                break;
            case 1:
                snprintf(line2, MAX_CHARS, "El valor es: %d", index_num);
                set_lcd_text("Menu 1", line2);
                break;
        }
        xSemaphoreGive(mutex_i2c);

        vTaskDelay(pdMS_TO_TICKS(SLEEP_TIME_LCD));
    }
}

void task_read_adc(void *pvParameters) {
    adc_init();
    adc_gpio_init(26);
    adc_gpio_init(27);
    const float convert_factor = 3.3f / (1 << 12); // Factor de conversión para 12 bits
    const float gain_current = 1 / (1 + 51.0f / 20.0f);
    const float gain_voltage = (22.0f + 68.0f) / 22.0f;

    while(1) {
        adc_select_input(0);
        float current_a = 10 * gain_current * (convert_factor * adc_read());

        adc_select_input(1);
        float vin_v = gain_voltage * (convert_factor * adc_read());

        float resistance = vin_v / current_a;

        float error = resistance_target - resistance;
    }

}

int main()
{
    stdio_init_all();
    // Inicializacion de Semaforo y Cola
    mutex_i2c = xSemaphoreCreateMutex();
    bin_btn_1 = xSemaphoreCreateBinary();
    bin_btn_2 = xSemaphoreCreateBinary();
    struct btn_data_t btn_data_1 = {
        .gpio = BTN_MENU_GPIO,
        .sem_bin = &bin_btn_1,
        .counter = &menu_num,
        .max_counter = MAX_MENU_NUM
    };
    struct btn_data_t btn_data_2 = {
        .gpio = BTN_STOP_GPIO,
        .sem_bin = &bin_btn_2,
        .counter = &index_num,
        .max_counter = 11
    };

    // Inicializacion del I2C. Freq 400Khz.
    i2c_init(I2C_PORT, 400*1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Creacion de tareas
    xTaskCreate(task_led_error, "task_led_error", configMINIMAL_STACK_SIZE * 1, NULL, 2, NULL);
    xTaskCreate(task_btn_pull_up, "task_btn_menu", configMINIMAL_STACK_SIZE * 1, &btn_data_1, 2, NULL);
    xTaskCreate(task_btn_pull_up, "task_btn_stop", configMINIMAL_STACK_SIZE * 1, &btn_data_2, 2, NULL);
    xTaskCreate(task_lcd_display, "task_lcd_display", configMINIMAL_STACK_SIZE * 1, NULL, 1, NULL);

    vTaskStartScheduler();
    while(true);
}

void set_lcd_text(const char *line1, const char *line2) {
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_string(line1);

    lcd_set_cursor(1, 0);
    lcd_string(line2);
}


void setup_pwm(uint8_t gpio) {
    // Asigna función de PWM
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    // Configura frecuencia de PWM e inicializa
    uint32_t slice = pwm_gpio_to_slice_num(gpio);
    pwm_set_clkdiv(slice, 1.25);
    pwm_set_wrap(slice, MAX_PWM_DUTY);
    pwm_set_gpio_level(gpio, 0);
    pwm_set_enabled(slice, true);

}