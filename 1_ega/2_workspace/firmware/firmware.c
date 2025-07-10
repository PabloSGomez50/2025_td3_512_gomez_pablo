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

#define SLEEP_TIME_LCD 500 // Tiempo de espera en ms para la LCD

// Botones y Encoder
#define DEBOUNCE_TIME 50 // Tiempo de debounce en ms
#define BTN_MENU_GPIO 14
#define BTN_STOP_GPIO 13
#define MAX_MENU_NUM 2

#define ENC_CHA_GPIO 2
#define ENC_CHB_GPIO 3
#define ENC_MAX_INDEX 45

// Controlador PID y protecciones
#define MAX_PWM_DUTY 1024
#define PWM_PIN 15
#define TEMP_THRESHOLD 100.0f
#define Kp 15
#define Kd 4
#define CONTROLLER_REFRESH_MS 5

#define MINIMUM_RESISTANCE 2.0f


// Mutex para sincronizacion de tareas
SemaphoreHandle_t mutex_i2c;
SemaphoreHandle_t bin_btn_1;
SemaphoreHandle_t bin_btn_2;

QueueHandle_t queue_encoder;
QueueHandle_t queue_adc_data;


struct adc_data_t {
    float current_ma;
    float vin_v;
};

struct btn_data_t {
    uint8_t gpio;
    SemaphoreHandle_t *sem_bin;
    uint8_t * counter;
    uint8_t max_counter;
};

typedef struct encoder_t {
    bool cha;
    bool chb;
} encoder_t;

uint8_t menu_num = 0, start_num = 0;
volatile uint8_t index_num = 0;
float resistance_target = 500.0f;

void setup_pwm(uint8_t gpio);
void set_lcd_text(const char *line1, const char *line2);

void btn_irq_handler(uint gpio, uint32_t events) {
    static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    encoder_t encoder;
    if (gpio == ENC_CHA_GPIO) {
        encoder.cha = gpio_get(ENC_CHA_GPIO);
        encoder.chb = gpio_get(ENC_CHB_GPIO);
        xQueueOverwriteFromISR(queue_encoder, &encoder, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        return;
    }

    gpio_set_irq_enabled(gpio, events, false);

    if (gpio == BTN_MENU_GPIO) {
        xSemaphoreGiveFromISR(bin_btn_1, &xHigherPriorityTaskWoken);       
    }
    if (gpio == BTN_STOP_GPIO) {
        xSemaphoreGiveFromISR(bin_btn_2, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}


void task_encoder(void *pvParameters) {
    gpio_init(ENC_CHA_GPIO);
    gpio_set_dir(ENC_CHA_GPIO, GPIO_IN);
    gpio_set_irq_enabled_with_callback(
        ENC_CHA_GPIO,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
        true,
        btn_irq_handler
    );

    gpio_init(ENC_CHB_GPIO);
    gpio_set_dir(ENC_CHB_GPIO, GPIO_IN);

    #define LED_PIN_CHA 12
    #define LED_PIN_CHB 11

    // gpio_init(LED_PIN_CHA);
    // gpio_set_dir(LED_PIN_CHA, GPIO_OUT);
    // gpio_init(LED_PIN_CHB);
    // gpio_set_dir(LED_PIN_CHB, GPIO_OUT);

    encoder_t enc_status = {0};

    while(1) {
        xQueueReceive(queue_encoder, &enc_status, portMAX_DELAY);
        // gpio_put(LED_PIN_CHA, enc_status.cha);
        // gpio_put(LED_PIN_CHB, enc_status.chb);

        if (enc_status.cha != enc_status.chb)
            index_num = (index_num + 1) % ENC_MAX_INDEX;
        else
            index_num = (index_num + ENC_MAX_INDEX - 1) % ENC_MAX_INDEX;
        vTaskDelay(pdMS_TO_TICKS(10)); // Espera para evitar rebotes
    }
}


void task_btn_pull_up(void *pvParameters) {
    struct btn_data_t * btn_data = (struct btn_data_t *) pvParameters;
    gpio_init(btn_data->gpio);
    gpio_set_dir(btn_data->gpio, GPIO_IN);
    gpio_pull_up(btn_data->gpio);
    gpio_set_irq_enabled_with_callback(btn_data->gpio, GPIO_IRQ_EDGE_RISE, false, btn_irq_handler);
    gpio_set_irq_enabled_with_callback(btn_data->gpio, GPIO_IRQ_EDGE_FALL, true, btn_irq_handler);

    while(1) {
        xSemaphoreTake(*btn_data->sem_bin, portMAX_DELAY);
        *btn_data->counter = (*btn_data->counter + 1) % btn_data->max_counter;
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME));

        if (!gpio_get(btn_data->gpio)) {
            gpio_set_irq_enabled(btn_data->gpio, GPIO_IRQ_EDGE_RISE, true);
            xSemaphoreTake(*btn_data->sem_bin, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME));
        }
        gpio_set_irq_enabled(btn_data->gpio, GPIO_IRQ_EDGE_FALL, true);
    }
}

void task_enc_controller(void *pvParameters) {
    // Esta tarea no se usa en este ejemplo, pero se puede implementar si se desea
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME)); // Simula un delay
    }
}


void task_error_controller(void *pvParameters) {

    // Configuración del PWM
    setup_pwm(PWM_PIN);
    struct adc_data_t adc_data;
    float resistance = 0.0f;
    float error = 0.0f;
    float last_error = 0.0f;
    vTaskSuspend(NULL);
    while(1) {
        xQueueReceive(queue_adc_data, &adc_data, portMAX_DELAY);
        float resistance = 1000 * adc_data.vin_v / adc_data.current_ma; // Convertir a Ohmios
        if (resistance < MINIMUM_RESISTANCE) {
            resistance = MINIMUM_RESISTANCE;
        }
        float error = resistance_target - resistance;
        last_error = error;
        int16_t duty = Kp * error + Kd * (error - last_error) / CONTROLLER_REFRESH_MS;

        if (duty < 0) {
            duty *= -1;
        }
        if (duty > MAX_PWM_DUTY) {
            duty = MAX_PWM_DUTY; // Limita el duty cycle al máximo
        }
        pwm_set_gpio_level(PWM_PIN, duty);
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
                snprintf(line2, MAX_CHARS, "| |Reg. %d", index_num);
                set_lcd_text("| |Resistencia", line2);
                break;
            case 1:
                snprintf(line2, MAX_CHARS, "El valor es: %d", start_num);
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
    // adc_set_round_robin();
    const float convert_factor = 3.3f / (1 << 12); // Factor de conversión para 12 bits
    // const float gain_current = 1 / (1 + 51.0f / 20.0f);
    // const float gain_voltage = (22.0f + 68.0f) / 22.0f;
    const float gain_current = 1.0f; // Ganancia de corriente
    const float gain_voltage = 1.0f; // Ganancia de voltaje

    struct adc_data_t adc_data = {
        .current_ma = 0.0f,
        .vin_v = 0.0f
    };
    vTaskSuspend(NULL);

    while(1) {
        adc_select_input(0);
        adc_data.current_ma = gain_current * (convert_factor * adc_read());
        
        adc_select_input(1);
        adc_data.vin_v = gain_voltage * (convert_factor * adc_read());

        xQueueSend(queue_adc_data, &adc_data, portMAX_DELAY);
    }

}

int main()
{
    stdio_init_all();
    // Inicializacion de Semaforo y Cola
    queue_adc_data = xQueueCreate(10, sizeof(struct adc_data_t));
    if (queue_adc_data == NULL) {
        printf("Error al crear la cola de datos ADC\n");
        return -1;
    }
    queue_encoder = xQueueCreate(1, sizeof(encoder_t));
    if (queue_encoder == NULL) {
        printf("Error al crear la cola del encoder\n");
        return -1;
    }

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
        .counter = &start_num,
        .max_counter = 12
    };

    // Inicializacion del I2C. Freq 400Khz.
    i2c_init(I2C_PORT, 400*1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Creacion de tareas
    // xTaskCreate(task_error_controller, "task_error_controller", configMINIMAL_STACK_SIZE * 1, NULL, 2, NULL);
    xTaskCreate(task_encoder, "task_encoder", configMINIMAL_STACK_SIZE * 1, NULL, 2, NULL);
    xTaskCreate(task_btn_pull_up, "task_btn_menu", configMINIMAL_STACK_SIZE * 1, &btn_data_1, 2, NULL);
    xTaskCreate(task_btn_pull_up, "task_btn_stop", configMINIMAL_STACK_SIZE * 1, &btn_data_2, 2, NULL);
    xTaskCreate(task_lcd_display, "task_lcd_display", configMINIMAL_STACK_SIZE * 1, NULL, 3, NULL);
    xTaskCreate(task_read_adc, "task_read_adc", configMINIMAL_STACK_SIZE * 1, NULL, 1, NULL);

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
    pwm_set_clkdiv(slice, 1.4648);
    pwm_set_wrap(slice, MAX_PWM_DUTY);
    pwm_set_gpio_level(gpio, 0);
    pwm_set_enabled(slice, true);

}