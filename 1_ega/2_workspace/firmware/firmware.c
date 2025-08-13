#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "lcd.h"
#include "ina219.h"

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
#define ENC_MAX_INDEX 100

// Controlador PID y protecciones
#define MAX_PWM_DUTY 1024
#define PWM_PIN 15
#define ADC_DIODE_TEMP 0 // Pin 26
#define TEMP_THRESHOLD 100.0f
#define Kp 15
#define Kd 4
#define Ki 3.5
#define MAX_INTEGRAL_VALUE 1000.0f
#define CONTROLLER_REFRESH_MS 5

#define MINIMUM_RESISTANCE 2.0f

void btn_irq_handler(uint gpio, uint32_t events);
void task_encoder(void *pvParameters);
void task_btn_pull_up(void *pvParameters);

uint8_t menu_num = 0, start_num = 0;
uint8_t index_num = 0, index_max = ENC_MAX_INDEX;
float resistance_target = 500.0f;

void setup_pwm(uint8_t gpio);
void set_lcd_text(void *text);

// Mutex para sincronizacion de tareas
SemaphoreHandle_t bin_btn_1;
SemaphoreHandle_t bin_btn_2;

QueueHandle_t queue_encoder;
QueueHandle_t queue_ina219_data;
QueueHandle_t queue_i2c_guard;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} rtc_data_t;

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

enum i2c_devices_t {
    I2C_INA219,
    I2C_LCD,
    I2C_RTC
};

typedef struct {
    enum i2c_devices_t device;
    QueueHandle_t queue;
    void (*callback)(void * param);
    void * param;
} i2c_guard_t;


void task_i2c_guard(void *pvParameters) {
    i2c_guard_t guard_data = {0};
    while(1) {
        xQueueReceive(queue_i2c_guard, &guard_data, portMAX_DELAY);

        switch(guard_data.device) {
            case I2C_LCD:
                guard_data.callback(guard_data.param);
                break;

            case I2C_INA219:
                if (guard_data.queue != NULL) {
                    guard_data.callback(guard_data.param);
                    ina219_context_t *context = (ina219_context_t *) guard_data.param;
                    xQueueSend(guard_data.queue, context->data, 1);
                } else {
                    guard_data.callback(guard_data.param);
                }
                break;
            case I2C_RTC:

                break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}



void task_ina219(void *pvParameters) {
    ina219_t ina219 = ina219_get_default_config();
    ina219.i2c = I2C_PORT;
    ina219._max_expected_amps = 0.3f;
    ina219._shunt_resistor_value = 0.1f;

    ina219_data_t ina219_data = {0};
    ina219_context_t context = {
        .data = &ina219_data,
        .ina219 = ina219
    };

    i2c_guard_t guard_data;
    guard_data = (i2c_guard_t) {
        .device = I2C_INA219,
        .queue = NULL,
        .callback = ina219_init_and_calibrate,
        .param = (void *) &context
    };
    xQueueSend(
        queue_i2c_guard,
        &guard_data,
        portMAX_DELAY
    );

    guard_data.callback = ina219_get_data;
    guard_data.queue = queue_ina219_data;
    while(1) {
        xQueueSend(
            queue_i2c_guard,
            &guard_data,
            portMAX_DELAY
        );

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void task_pid_controller(void *pvParameters) {

    // Configuración del PWM
    setup_pwm(PWM_PIN);
    ina219_data_t ina219_data;
    float resistance = 0.0f;
    float error = 0.0f;
    float last_error = 0.0f;

    uint32_t ticks = xTaskGetTickCount();
    uint32_t last_ticks = ticks;
    uint32_t elapsed_ticks = 0;

    float integral_value = 0.0f;

    while(1) {
        xQueueReceive(queue_ina219_data, &ina219_data, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(2000)); // Espera para estabilizar la lectura
        continue;

        float resistance = ina219_data.voltage_v / ina219_data.current_a;
        if (resistance < MINIMUM_RESISTANCE) {
            resistance = MINIMUM_RESISTANCE;
        }

        ticks = xTaskGetTickCount();
        elapsed_ticks = ticks - last_ticks;
        last_ticks = ticks;

        float error = resistance_target - resistance;
        last_error = error;

        integral_value += error * elapsed_ticks;
        if (integral_value > MAX_INTEGRAL_VALUE) {
            integral_value = MAX_INTEGRAL_VALUE;
        } else if (integral_value < -MAX_INTEGRAL_VALUE) {
            integral_value = -MAX_INTEGRAL_VALUE;
        }

        int16_t duty = (int16_t) (Kp * error + Kd * (error - last_error) / elapsed_ticks + Ki * integral_value);

        if (duty < 0) {
            duty *= -1;
        }
        if (duty > MAX_PWM_DUTY) {
            duty = MAX_PWM_DUTY;
        }
        pwm_set_gpio_level(PWM_PIN, duty);
        // Minima espera entre refresh
        vTaskDelay(pdMS_TO_TICKS(CONTROLLER_REFRESH_MS));
    }
}

void task_lcd_display(void *pvParameters) {
    // Inicializacion del LCD
    lcd_init(I2C_PORT, LCD_ADDR);
    char line1[MAX_CHARS * 2];
    char * line2 = line1 + MAX_CHARS;
    i2c_guard_t guard_data = {
        .device = I2C_LCD,
        .queue = NULL,
        .callback = set_lcd_text,
        .param = (void *) line1
    };

    ina219_data_t ina219_data = {0};
    

    while(1) {

        switch(menu_num) {
            case 0:
                snprintf(line1, MAX_CHARS, "|%c|R cte.", index_num == 0 ? 'X' : ' ');
                snprintf(line2, MAX_CHARS, "|%c|Reg. %d", index_num == 1 ? 'X' : ' ', index_num);

                break;
            case 1:
                BaseType_t status = xQueueReceive(queue_ina219_data, &ina219_data, 10);
                if (status != pdTRUE) {
                    snprintf(line1, MAX_CHARS, "Error al leer");
                    snprintf(line2, MAX_CHARS, "INA219");
                    break;
                }
                snprintf(line1, MAX_CHARS, "Vmed: %.2f V", ina219_data.voltage_v);
                snprintf(line2, MAX_CHARS, "Imed: %.2f A", ina219_data.current_a);

                break;
        }
        xQueueSend(
            queue_i2c_guard,
            &guard_data,
            portMAX_DELAY
        );

        vTaskDelay(pdMS_TO_TICKS(SLEEP_TIME_LCD));
    }
}

void task_read_temp(void *pvParameters) {
    const float convert_factor = 3.3f / (1 << 12); // Factor de conversión para 12 bits

    // Configuracion de ADC
    adc_init();
    adc_gpio_init(26 + ADC_DIODE_TEMP);
    // Amplificador de ganancia 4.7
    while(1) {
        float v_temp = adc_read() * convert_factor;
    }
}

int main()
{
    stdio_init_all();
    // Inicializacion de Semaforo y Cola
    queue_ina219_data = xQueueCreate(3, sizeof(ina219_data_t));
    if (queue_ina219_data == NULL) {
        printf("Error al crear la cola de datos INA219\n");
        return -1;
    }
    queue_encoder = xQueueCreate(1, sizeof(encoder_t));
    if (queue_encoder == NULL) {
        printf("Error al crear la cola del encoder\n");
        return -1;
    }
    queue_i2c_guard = xQueueCreate(5, sizeof(i2c_guard_t));
    if (queue_i2c_guard == NULL) {
        printf("Error al crear la cola de guardia I2C\n");
        return -1;
    }

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
    i2c_init(I2C_PORT, 100*1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Creacion de tareas
    xTaskCreate(task_pid_controller, "task_pid_controller", configMINIMAL_STACK_SIZE * 1, NULL, 3, NULL);
    xTaskCreate(task_i2c_guard, "task_i2c_guard", configMINIMAL_STACK_SIZE * 4, NULL, 3, NULL);
    xTaskCreate(task_ina219, "task_ina219", configMINIMAL_STACK_SIZE * 3, NULL, 2, NULL);
    xTaskCreate(task_encoder, "task_encoder", configMINIMAL_STACK_SIZE * 1, NULL, 2, NULL);
    xTaskCreate(task_btn_pull_up, "task_btn_menu", configMINIMAL_STACK_SIZE * 1, &btn_data_1, 1, NULL);
    xTaskCreate(task_btn_pull_up, "task_btn_stop", configMINIMAL_STACK_SIZE * 1, &btn_data_2, 1, NULL);
    xTaskCreate(task_lcd_display, "task_lcd_display", configMINIMAL_STACK_SIZE * 2, NULL, 2, NULL);
    // xTaskCreate(task_read_temp, "task_read_temp", configMINIMAL_STACK_SIZE * 1, NULL, 1, NULL);

    vTaskStartScheduler();
    while(true);
}

void set_lcd_text(void *str) {
    char *text = (char *) str;
    char line1[MAX_CHARS], line2[MAX_CHARS];
    strncpy(line1, text, MAX_CHARS);
    strncpy(line2, text + MAX_CHARS, MAX_CHARS);

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
    encoder_t enc_status = {0};

    while(1) {
        xQueueReceive(queue_encoder, &enc_status, portMAX_DELAY);
        if (enc_status.cha != enc_status.chb)
            index_num = (index_num + 1) % index_max;
        else
            index_num = (index_num + index_max - 1) % index_max;
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