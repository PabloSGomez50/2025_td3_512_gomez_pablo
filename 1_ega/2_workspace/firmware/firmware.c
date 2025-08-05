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

#include "config.h"

// Mutex para sincronizacion de tareas
SemaphoreHandle_t bin_btn_1;
SemaphoreHandle_t bin_btn_2;
SemaphoreHandle_t bin_btn_3;

QueueHandle_t queue_encoder;
QueueHandle_t queue_ina219_data;
QueueHandle_t queue_i2c_guard;
QueueHandle_t queue_input_data;

system_config_t system_config = {
    .menu = 0,
    .pid_enabled = false,
    .resistance_target = 500.0f, // Valor inicial de resistencia objetivo
    .pid_stable = false,
    .ina219_data = {0}
};

void task_main(void *pvParameters) {
    input_data_t input_data = {0};
    ina219_data_t ina219_data = {0};
    static bool fixed_index = false;

    while(1) {
        xQueueReceive(queue_input_data, &input_data, portMAX_DELAY);
        BaseType_t status = xQueueReceive(queue_ina219_data, &ina219_data, 1);
        if (status == pdTRUE) {
            system_config.ina219_data = ina219_data;
        }

        if (input_data.device == BTN_STOP) {
            system_config.pid_enabled = !system_config.pid_enabled;
            continue;
        }

        if (system_config.menu == 0) {
            if (input_data.device == ENCODER) {
                system_config.index = (system_config.index + 1) % 2;
            }
            if (input_data.device == BTN_SWITCH) {
                switch (system_config.index) {
                    case 0:
                    system_config.menu = 1;
                    break;
                    case 1:
                    system_config.menu = 3;
                    break;
                }
                system_config.index = 0;
                system_config.pid_enabled = false;
                system_config.resistance_adj = MINIMUM_RESISTANCE;
            }
            continue;
        }

        if (system_config.menu == 1) {

            if (input_data.device == ENCODER) {
                if (fixed_index) {
                    switch (system_config.index) {
                        case 0:
                            system_config.resistance_adj += input_data.increment ? 0.5f : -0.5f;
                            if (system_config.resistance_adj < MINIMUM_RESISTANCE) {
                                system_config.resistance_adj = MINIMUM_RESISTANCE;
                            }
                            break;
                        case 1:
                            system_config.pid_escalon = !system_config.pid_escalon;
                            break;
                    }
                } else {
                    system_config.index = (system_config.index + (input_data.increment ? 1 : 2)) % 3;
                }
            }

            if (input_data.device == BTN_SWITCH) {
                if (!fixed_index) {
                    if (system_config.index == 2) {
                        system_config.index = 0;
                        system_config.menu = 2;
                    } else {
                        fixed_index = true;
                    }
                }
                else {
                    fixed_index = false;
                    if (system_config.index == 0) {
                        system_config.resistance_target = system_config.resistance_adj;
                    }
                }
            }
            continue;
        }


        if (system_config.menu == 3 || system_config.menu == 2) {
            if (input_data.device == BTN_SWITCH) {
                system_config.menu = 0;
                system_config.index = 0;
            }
            continue;
        }
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

        system_config.ina219_data = ina219_data;
        
        if (ina219_data.current_a == 0.0f) {
            printf("Error: Current is zero, skipping PID calculation.\n");
            continue;
        }
        resistance = ina219_data.voltage_v / ina219_data.current_a;
        if (resistance < MINIMUM_RESISTANCE) {
            resistance = MINIMUM_RESISTANCE;
        }

        ticks = xTaskGetTickCount();
        elapsed_ticks = ticks - last_ticks;
        last_ticks = ticks;

        error = system_config.resistance_target - resistance;
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
    char *line2 = line1 + MAX_CHARS;
    i2c_guard_t guard_data = {
        .device = I2C_LCD,
        .queue = NULL,
        .callback = set_lcd_text,
        .param = (void *) line1
    };

    while(1) {

        switch(system_config.menu) {
            case 0:
                snprintf(line1, MAX_CHARS, "%cRcte|PID:%s", system_config.index == 0 ? 'X' : ' ', system_config.pid_enabled ? "ON" : "OFF");
                snprintf(line2, MAX_CHARS, "%cINA219", system_config.index == 1 ? 'X' : ' ');

                break;
            case 1:
                snprintf(line1, MAX_CHARS, "%cRadj: %.2f", system_config.index == 0 ? 'X' : ' ', system_config.resistance_adj);
                snprintf(line2, MAX_CHARS, "%cTip:%s|%cSig",
                    system_config.index == 1 ? 'X' : ' ',
                    system_config.pid_escalon ? "u(t)" : "r(t)",
                    system_config.index == 2 ? 'X' : ' '
                );

                break;
            case 2:
                snprintf(line1, MAX_CHARS, "Robj: %.2f", system_config.resistance_target);
                snprintf(line2, MAX_CHARS, "Pid: %s", system_config.pid_enabled ? "ON" : "OFF");

                break;
            case 3:
                snprintf(line1, MAX_CHARS, "Vmed: %.2f V", system_config.ina219_data.voltage_v);
                snprintf(line2, MAX_CHARS, "Imed: %.2f mA", system_config.ina219_data.current_a * 1000.0f);

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
    queue_input_data = xQueueCreate(5, sizeof(input_data_t));
    if (queue_input_data == NULL) {
        printf("Error al crear la cola de input_data\n");
        return -1;
    }

    bin_btn_1 = xSemaphoreCreateBinary();
    bin_btn_2 = xSemaphoreCreateBinary();
    bin_btn_3 = xSemaphoreCreateBinary();

    btn_data_t btn_data_1 = {
        .gpio = BTN_MENU_GPIO,
        .sem_bin = &bin_btn_1,
        .device = BTN_MENU,
        // .counter = &menu_num,
        // .max_counter = MAX_MENU_NUM
    };
    btn_data_t btn_data_2 = {
        .gpio = BTN_STOP_GPIO,
        .sem_bin = &bin_btn_2,
        .device = BTN_STOP,
        // .counter = &start_num,
        // .max_counter = 12
    };
    btn_data_t btn_data_3 = {
        .gpio = BTN_SWITCH_GPIO,
        .sem_bin = &bin_btn_3,
        .device = BTN_SWITCH,
        // .counter = &test_num,
        // .max_counter = 10
    };

    // Inicializacion del I2C. Freq 400Khz.
    i2c_init(I2C_PORT, 100*1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Creacion de tareas
    xTaskCreate(task_main, "task_main", configMINIMAL_STACK_SIZE * 4, NULL, 2, NULL);
    xTaskCreate(task_pid_controller, "task_pid_controller", configMINIMAL_STACK_SIZE * 1, NULL, 3, NULL);
    xTaskCreate(task_i2c_guard, "task_i2c_guard", configMINIMAL_STACK_SIZE * 4, NULL, 3, NULL);
    xTaskCreate(task_encoder, "task_encoder", configMINIMAL_STACK_SIZE * 1, NULL, 3, NULL);
    xTaskCreate(task_lcd_display, "task_lcd_display", configMINIMAL_STACK_SIZE * 3, NULL, 2, NULL);
    xTaskCreate(task_ina219, "task_ina219", configMINIMAL_STACK_SIZE * 3, NULL, 2, NULL);
    xTaskCreate(task_btn_pull_up, "task_btn_menu", configMINIMAL_STACK_SIZE * 1, &btn_data_1, 1, NULL);
    xTaskCreate(task_btn_pull_up, "task_btn_stop", configMINIMAL_STACK_SIZE * 1, &btn_data_2, 1, NULL);
    xTaskCreate(task_btn_pull_up, "task_btn_clk", configMINIMAL_STACK_SIZE * 1, &btn_data_3, 1, NULL);
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

    if (gpio == BTN_SWITCH_GPIO) {
        xSemaphoreGiveFromISR(bin_btn_3, &xHigherPriorityTaskWoken);
    }
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
    input_data_t input_data = {
        .device = ENCODER,
        .increment = true
    };

    while(1) {
        xQueueReceive(queue_encoder, &enc_status, portMAX_DELAY);
        if (enc_status.cha != enc_status.chb)
            // index_num = (index_num + 1) % index_max;
            input_data.increment = true;
        else
            // index_num = (index_num + index_max - 1) % index_max;
            input_data.increment = false;
        xQueueSendToBack(
            queue_input_data,
            &input_data,
            portMAX_DELAY
        );
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME));
    }
}


void task_btn_pull_up(void *pvParameters) {
    btn_data_t * btn_data = (btn_data_t *) pvParameters;
    input_data_t input_data = {
        .device = btn_data->device,
        .increment = true
    };
    gpio_init(btn_data->gpio);
    gpio_set_dir(btn_data->gpio, GPIO_IN);
    gpio_pull_up(btn_data->gpio);
    gpio_set_irq_enabled_with_callback(btn_data->gpio, GPIO_IRQ_EDGE_RISE, false, btn_irq_handler);
    gpio_set_irq_enabled_with_callback(btn_data->gpio, GPIO_IRQ_EDGE_FALL, true, btn_irq_handler);

    while(1) {
        xSemaphoreTake(*btn_data->sem_bin, portMAX_DELAY);
        // *btn_data->counter = (*btn_data->counter + 1) % btn_data->max_counter;
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME));

        if (!gpio_get(btn_data->gpio)) {
            gpio_set_irq_enabled(btn_data->gpio, GPIO_IRQ_EDGE_RISE, true);
            xSemaphoreTake(*btn_data->sem_bin, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME));
        }
        gpio_set_irq_enabled(btn_data->gpio, GPIO_IRQ_EDGE_FALL, true);
        xQueueSendToBack(
            queue_input_data,
            &input_data,
            portMAX_DELAY
        );
    }
}

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
        vTaskDelay(pdMS_TO_TICKS(SLEEP_I2C_GUARD));
    }
}

void task_ina219(void *pvParameters) {
    ina219_t ina219 = ina219_get_default_config();
    ina219.i2c = I2C_PORT;
    ina219._max_expected_amps = MAX_CURRENT_INA219;
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

        vTaskDelay(pdMS_TO_TICKS(SLEEP_INA219));
    }
}