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
QueueHandle_t queue_rtc_time;
QueueHandle_t queue_datalogger;

int16_t pwm_test_wrap = MIN_PWM_DUTY;
uint8_t resistance_step = 10;

system_config_t system_config = {
    .menu = MENU_MAIN,
    .index = 0,
    .fixed_index = false,
    .pid_enabled = false,
    .resistance_target = 500,
    .pid_stable = true,
    .sd_mounted = false,
};

void task_main(void *pvParameters) {
    input_data_t input_data = {0};

    while(1) {
        xQueueReceive(queue_input_data, &input_data, portMAX_DELAY);

        if (input_data.device == BTN_STOP) {
            system_config.pid_enabled = !system_config.pid_enabled;
            continue;
        }
        
        
        if (input_data.device == BTN_MENU) {
            system_config.menu = MENU_MAIN;
            system_config.index = 0;
            system_config.pid_enabled = false;
            continue;
        }

        if (system_config.menu == MENU_MAIN) {
            if (input_data.device == ENCODER) {
                system_config.index = (input_data.increment ? system_config.index + 1 : system_config.index + 5) % 6;
            }
            if (input_data.device == BTN_SWITCH) {
                system_config.menu = system_config.index;
                system_config.index = 0;
                system_config.pid_enabled = false;
                system_config.resistance_adj = MINIMUM_RESISTANCE;
            }
            continue;
        }

        if (system_config.menu == MENU_SET_RESISTANCE) {

            if (input_data.device == ENCODER) {
                if (!system_config.fixed_index) {
                    system_config.index = (system_config.index + (input_data.increment ? 1 : 3)) % 4;
                    continue;
                }
                switch (system_config.index) {
                    case 0:
                        if (resistance_step <= MINIMUM_RESISTANCE && !input_data.increment) {
                            resistance_step = MINIMUM_RESISTANCE;
                        } else {
                            resistance_step += input_data.increment ? RESISTANCE_STEP : -RESISTANCE_STEP;
                        }
                        break;
                    case 1:
                        if (system_config.resistance_adj < (resistance_step + MINIMUM_RESISTANCE) && !input_data.increment) {
                            system_config.resistance_adj = MINIMUM_RESISTANCE;
                        } else {
                            system_config.resistance_adj += input_data.increment ? resistance_step : -resistance_step;
                        }
                        break;
                    case 2:
                        system_config.pid_escalon = !system_config.pid_escalon;
                        break;
                }
            }

            if (input_data.device == BTN_SWITCH) {
                if (!system_config.fixed_index) {
                    if (system_config.index == 3) {
                        system_config.index = 0;
                        system_config.menu = MENU_PID_TUNING;
                    } else {
                        system_config.fixed_index = true;
                    }
                }
                else {
                    system_config.fixed_index = false;
                    if (system_config.index == 0) {
                        system_config.resistance_target = system_config.resistance_adj;
                    }
                }
            }
            continue;
        }

        if (system_config.menu == MENU_TEST) {
            if (input_data.device == ENCODER) {
                int16_t aux_wrap = (pwm_test_wrap + (input_data.increment ? 50 : -5));
                if (aux_wrap > MAX_PWM_DUTY) {
                    aux_wrap = MAX_PWM_DUTY;
                }
                if (aux_wrap < 0) {
                    aux_wrap = 0;
                }
                pwm_test_wrap = aux_wrap;
            }
            continue;
        }

    }
}

void task_lcd_display(void *pvParameters) {
    // Inicializacion del LCD
    char line1[MAX_CHARS * 2];
    char *line2 = line1 + MAX_CHARS;
    struct lcd_config lcd_config = {
        .i2c = I2C_PORT,
        .addr = LCD_ADDR
    };
    i2c_guard_t guard_data = {
        .device = I2C_LCD,
        .queue = NULL,
        .callback = lcd_init_rtos,
        .param = (void *) &lcd_config
    };
    xQueueSend(queue_i2c_guard, &guard_data, portMAX_DELAY);
    guard_data.callback = set_lcd_text;
    guard_data.param = (void *) line1;

    ina219_data_t ina219_data;
    static bool blink_state = false;

    static const char *main_options[] = {
        "Rcte",
        "Multiples sets",
        "INA219 (TEST)",
        "Time",
        "SD menu",
        "Reg Fuente"
    };
    const int menu_count = sizeof(main_options) / sizeof(main_options[0]);
    const uint8_t max_format_chars = MAX_CHARS + 1;
    while(1) {
        switch(system_config.menu) {
            case MENU_MAIN:
                // Calcula índice actual y el siguiente (scroll circular)
                int idx1 = system_config.index % menu_count;
                int idx2 = (system_config.index + 1) % menu_count;
                snprintf(line1, max_format_chars, "%c%s",
                    (system_config.index == idx1 & blink_state) ? '>' : ' ',
                    main_options[idx1]);
                snprintf(line2, max_format_chars, "%c%s",
                    (system_config.index == idx2 & blink_state) ? '>' : ' ',
                    main_options[idx2]);
                break;
            case MENU_SET_RESISTANCE:
                snprintf(line1, max_format_chars, "%cPaso:%3d%cR:%4d",
                    (system_config.index == 0 & blink_state) ? '>' : ' ',
                    resistance_step,
                    (system_config.index == 1 & blink_state) ? '>' : ' ',
                    system_config.resistance_adj
                );
                snprintf(line2, max_format_chars, "%cTipo:%s%cSig.",
                    (system_config.index == 2 & blink_state) ? '>' : ' ',
                    system_config.pid_escalon ? "u(t)" : "r(t)",
                    (system_config.index == 3 & blink_state) ? '>' : ' '
                );
                break;
            case MENU_PID_TUNING:
                snprintf(line1, max_format_chars, "Robj: %5d Ohm", system_config.resistance_target);
                snprintf(line2, max_format_chars, "Pid: %s", system_config.pid_enabled ? "ON" : "OFF");
                break;
            case MENU_TEST:
                BaseType_t status = xQueuePeek(queue_ina219_data, &ina219_data, 1);
                if (status == pdFALSE) {
                    snprintf(line1, max_format_chars, "Error: INA219");
                    snprintf(line2, max_format_chars, "No data");
                } else {
                    snprintf(line1, max_format_chars, "W:%5d|I:%04.2fmA", pwm_test_wrap, ina219_data.current_a * 1000);
                    snprintf(line2, max_format_chars, "C:%s|V:%04.1fv", system_config.pid_enabled ? "ON " : "OFF", ina219_data.voltage_v);
                }

                break;
            case MENU_TIME:
                time_t current_time;
                BaseType_t queue_status = xQueuePeek(queue_rtc_time, &current_time, 1);
                if (queue_status == pdFALSE) {
                    snprintf(line1, max_format_chars, "Error: RTC");
                    snprintf(line2, max_format_chars, "No data");
                    break;
                }
                snprintf(line1, max_format_chars, "Time: %02d:%02d:%02d", current_time.hour, current_time.minute, current_time.second);
                snprintf(line2, max_format_chars, "Date: %04d-%02d-%02d", current_time.year, current_time.month, current_time.date);
                break;

            case MENU_SD:
                snprintf(line1, max_format_chars, "SD: %s", system_config.sd_mounted ? "Mounted" : "Unmounted");
                snprintf(line2, max_format_chars, "Index: %d", system_config.index);
                break;
            default:
                snprintf(line1, max_format_chars, "Menu: %d", system_config.menu);
                snprintf(line2, max_format_chars, "Index: %d", system_config.index);
                break;
        }
        xQueueSend(
            queue_i2c_guard,
            &guard_data,
            portMAX_DELAY
        );
        if (system_config.fixed_index)
            blink_state = true; // No parpadea si el índice es fijo
        else
            blink_state = !blink_state; // Alterna el estado de parpadeo

        vTaskDelay(pdMS_TO_TICKS(SLEEP_TIME_LCD));
    }
}

void task_pid_controller(void *pvParameters) {
    // Configuración del PWM
    setup_pwm(PWM_PIN);
    gpio_init(PID_STATUS_PIN);
    gpio_set_dir(PID_STATUS_PIN, GPIO_OUT);
    gpio_put(PID_STATUS_PIN, system_config.pid_enabled);
    ina219_data_t ina219_data;
    uint16_t resistance = 0;
    uint16_t pwm_value = 0;
    float error = 0;
    float last_error = 0;
    float integral_value = 0;
    
    sd_event_t sd_event = {
        .type = LOG_FILE,
        .data = {0}
    };
    uint8_t datalogger_index = 0;

    TickType_t ticks = xTaskGetTickCount();
    
    while(1) {
        vTaskDelayUntil(&ticks, pdMS_TO_TICKS(CONTROLLER_REFRESH_MS));
        if (!system_config.pid_enabled) {
            pwm_set_gpio_level(PWM_PIN, 0);
            gpio_put(PID_STATUS_PIN, false);
            continue;
        }
        if (system_config.pid_enabled && pwm_test_wrap > MIN_PWM_DUTY && pwm_test_wrap < MAX_PWM_DUTY) {
            pwm_set_gpio_level(PWM_PIN, pwm_test_wrap);
            gpio_put(PID_STATUS_PIN, true);
        } else {
            pwm_set_gpio_level(PWM_PIN, 0);
            gpio_put(PID_STATUS_PIN, false);
        }
        xQueuePeek(queue_ina219_data, &ina219_data, portMAX_DELAY);
        if (ina219_data.voltage_v >= MAX_VOLTAGE || ina219_data.current_a >= INA219_MAX_CURRENT) {
            printf("Error: Voltage or current out of range, skipping PID calculation.\n");
            system_config.pid_enabled = false;
            system_config.pid_stable = false;
            pwm_set_gpio_level(PWM_PIN, 0);
            continue;
        }

        datalogger_index++;
        if (datalogger_index >= 5) {
            sd_event.data = (datalogger_t) {
                .temperature = 50.0f,
                .current_ma = ina219_data.current_a * 1000.0f,
                .voltage_v = ina219_data.voltage_v,
                .resistance_target = system_config.resistance_target,
                .error = error,
                .pid_enabled = system_config.pid_enabled,
                .pid_stable = system_config.pid_stable
            };
            xQueueSend(queue_datalogger, &sd_event, 1);
            datalogger_index = 0;
        }
    }

    while(1) {
        vTaskDelayUntil(&ticks, pdMS_TO_TICKS(CONTROLLER_REFRESH_MS));
        gpio_put(PID_STATUS_PIN, system_config.pid_enabled);
        if (!system_config.pid_enabled) {
            pwm_set_gpio_level(PWM_PIN, 0);
            continue;
        }

        xQueuePeek(queue_ina219_data, &ina219_data, portMAX_DELAY);

        if (ina219_data.voltage_v >= MAX_VOLTAGE || ina219_data.current_a >= INA219_MAX_CURRENT) {
            printf("Error: Voltage or current out of range, skipping PID calculation.\n");
            system_config.pid_enabled = false;
            system_config.pid_stable = false;
            pwm_set_gpio_level(PWM_PIN, 0);
            continue;
        }
        
        if ((ina219_data.current_a * 1000.0f) <= 0.5f) {
            printf("Error: Current is zero, skipping PID calculation.\n");
            if (pwm_value < MIN_PWM_DUTY) {
                pwm_value = MIN_PWM_DUTY;
            } else {
                pwm_value += 3;
            }
            pwm_set_gpio_level(PWM_PIN, pwm_value);
            continue;
        }
        resistance = (float) (ina219_data.voltage_v / ina219_data.current_a);

        error = (float) system_config.resistance_target - resistance;
        
        integral_value += error * CONTROLLER_REFRESH_MS;
        if (integral_value > MAX_INTEGRAL_VALUE) {
            integral_value = MAX_INTEGRAL_VALUE;
        } else if (integral_value < -MAX_INTEGRAL_VALUE) {
            integral_value = -MAX_INTEGRAL_VALUE;
        }
        
        int16_t delta_duty = (int16_t) (
            Kp * error +
            Kd * (error - last_error) / CONTROLLER_REFRESH_MS +
            Ki * integral_value
        );
        last_error = error;
        pwm_value -= delta_duty;
        if (pwm_value < MIN_PWM_DUTY) {
            pwm_value = MIN_PWM_DUTY;
        }
        if (pwm_value > MAX_PWM_DUTY) {
            pwm_value = MAX_PWM_DUTY;
        }
        pwm_set_gpio_level(PWM_PIN, pwm_value);
        sd_event.data = (datalogger_t) {
            .temperature = 50.0f,
            .voltage_v = ina219_data.voltage_v,
            .current_ma = ina219_data.current_a * 1000.0f,
            .resistance_target = system_config.resistance_target,
            .error = error,
            .pid_enabled = system_config.pid_enabled,
            .pid_stable = system_config.pid_stable
        };
        xQueueSend(queue_datalogger, &sd_event, 1);
    }   
}

void task_read_temp(void *pvParameters) {
    const float convert_factor = 3.3f / (1 << 12); // Factor de conversión para 12 bits
    const float slope_temp = (0.6f - 0.245f) / (-20 - 140);
    // Configuracion de ADC
    adc_init();
    adc_gpio_init(26 + ADC_DIODE_TEMP);
    
    // Amplificador de ganancia 4.9
    while(1) {
        float v_temp = adc_read() * convert_factor / 4.9f;
        float temp = (v_temp - 0.245f) / slope_temp;
    }
}


void task_datalogger(void *pvParameters) {
    FATFS fs;
    FRESULT f_res;
    DIR dir;
    FILINFO fno;
    FIL fp;
    UINT bw; // Bytes escritos

    datalogger_t data_to_log[LOGGER_CHUNK_SIZE] = {0};
    uint8_t data_index = 0;
    sd_event_t sd_event = {0};
    char buffer[128] = {0};
    time_t current_time;
    
    while(1) {
        if (!system_config.sd_mounted) {
            f_res = f_mount(&fs, "", 1);
            if (f_res == FR_OK) {
                system_config.sd_mounted = true;
            }
        }

        while (system_config.sd_mounted) {
            if (!sd_card_alive()) {
                printf("Card removed\n");
                system_config.sd_mounted = false;
                break;
            }
            xQueueReceive(queue_datalogger, &sd_event, portMAX_DELAY);

            if (sd_event.type == LOG_FILE) {
                xQueuePeek(queue_rtc_time, &current_time, portMAX_DELAY);
                sd_event.data.time = current_time;
                data_to_log[data_index++] = sd_event.data;
                if (data_index >= LOGGER_CHUNK_SIZE) {
                    f_res = f_open(&fp, "log.csv", FA_WRITE | FA_OPEN_APPEND);
                    if (f_res != FR_OK) {
                        f_res = f_open(&fp, "log.csv", FA_WRITE | FA_CREATE_NEW);
                        if (f_res != FR_OK) {
                            system_config.sd_mounted = false;
                            continue;
                        }
                        snprintf(buffer, sizeof(buffer), "Time;Voltage;Current;Resistance;Error;PID Enabled;PID Stable\n");
                        f_res = f_write(&fp, buffer, strlen(buffer), &bw);
                    }
                    // El puntero esta en la posicion final del archivo
                    for (uint8_t i = 0; i < data_index; i++) {
                        snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d;%.2f;%.2f;%04d;%.2f;%01d;%01d\n",
                            data_to_log[i].time.hour,
                            data_to_log[i].time.minute,
                            data_to_log[i].time.second,
                            data_to_log[i].voltage_v,
                            data_to_log[i].current_ma,
                            data_to_log[i].resistance_target,
                            data_to_log[i].error,
                            data_to_log[i].pid_enabled ? 1 : 0,
                            data_to_log[i].pid_stable ? 1 : 0
                        );
                        f_res = f_write(&fp, buffer, strlen(buffer), &bw);
                        if (f_res != FR_OK) {
                            printf("Error writing to file: %d\n", f_res);
                            break;
                        }
                    }
                    data_index = 0;
                    f_close(&fp);
                }
            }
            else if (sd_event.type == CONFIG_FILE) {
                f_res = f_open(&fp, "config.txt", FA_WRITE | FA_CREATE_ALWAYS);
                if (f_res != FR_OK) {
                    system_config.sd_mounted = false;
                    continue;
                }
                snprintf(buffer, sizeof(buffer), "Menu: %d;Index: %d;Fixed Index: %d;SD Mounted: %d;PID Enabled: %d;PID Escalon: %d;PID Stable: %d;Resistance Target: %d;Resistance Adj: %d\n",
                    system_config.menu,
                    system_config.index,
                    system_config.fixed_index,
                    system_config.sd_mounted,
                    system_config.pid_enabled,
                    system_config.pid_escalon,
                    system_config.pid_stable,
                    system_config.resistance_target,
                    system_config.resistance_adj
                );
                f_res = f_write(&fp, buffer, strlen(buffer), &bw);
                if (f_res != FR_OK) {
                    printf("Error writing to file: %d\n", f_res);
                }
                f_close(&fp);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelay(pdMS_TO_TICKS(750));
    }
}

int main()
{
    stdio_init_all();
    // Inicializacion de Semaforo y Cola
    queue_ina219_data = xQueueCreate(1, sizeof(ina219_data_t));
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
    queue_rtc_time = xQueueCreate(1, sizeof(time_t));
    if (queue_rtc_time == NULL) {
        printf("Error al crear la cola de tiempo RTC\n");
        return -1;
    }

    queue_datalogger = xQueueCreate(3, sizeof(sd_event_t));
    if (queue_datalogger == NULL) {
        printf("Error al crear la cola de datalogger\n");
        return -1;
    }

    bin_btn_1 = xSemaphoreCreateBinary();
    bin_btn_2 = xSemaphoreCreateBinary();
    bin_btn_3 = xSemaphoreCreateBinary();

    btn_data_t btn_data_1 = {
        .gpio = BTN_MENU_GPIO,
        .sem_bin = &bin_btn_1,
        .device = BTN_MENU
    };
    btn_data_t btn_data_2 = {
        .gpio = BTN_STOP_GPIO,
        .sem_bin = &bin_btn_2,
        .device = BTN_STOP
    };
    btn_data_t btn_data_3 = {
        .gpio = BTN_SWITCH_GPIO,
        .sem_bin = &bin_btn_3,
        .device = BTN_SWITCH
    };

    // Inicializacion del I2C.
    i2c_init(I2C_PORT, 100*1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Creacion de tareas
    xTaskCreate(
        task_main, "task_main", configMINIMAL_STACK_SIZE * 4,
        NULL, 2, NULL
    );
    xTaskCreate(
        task_btn_pull_up, "task_btn_stop", configMINIMAL_STACK_SIZE * 1,
        &btn_data_2, 3, NULL
    );
    xTaskCreate(
        task_pid_controller, "task_pid_controller", configMINIMAL_STACK_SIZE * 2,
        NULL, 3, NULL
    );
    xTaskCreate(
        task_i2c_guard, "task_i2c_guard", configMINIMAL_STACK_SIZE * 3,
        NULL, 3, NULL
    );
    xTaskCreate(
        task_encoder, "task_encoder", configMINIMAL_STACK_SIZE * 1,
        NULL, 2, NULL
    );
    xTaskCreate(
        task_lcd_display, "task_lcd_display", configMINIMAL_STACK_SIZE * 3,
        NULL, 2, NULL
    );
    xTaskCreate(
        task_ina219, "task_ina219", configMINIMAL_STACK_SIZE * 3,
        NULL, 2, NULL
    );
    xTaskCreate(
        task_datalogger, "task_datalogger", configMINIMAL_STACK_SIZE * 6,
        NULL, 1, NULL
    );
    xTaskCreate(
        task_btn_pull_up, "task_btn_menu", configMINIMAL_STACK_SIZE * 1,
        &btn_data_1, 1, NULL
    );
    xTaskCreate(
        task_btn_pull_up, "task_btn_clk", configMINIMAL_STACK_SIZE * 1,
        &btn_data_3, 1, NULL
    );
    xTaskCreate(
        task_rtc, "task_rtc", configMINIMAL_STACK_SIZE * 1,
        NULL, 1, NULL
    );
    // xTaskCreate(task_read_temp, "task_read_temp", configMINIMAL_STACK_SIZE * 1, NULL, 1, NULL);

    vTaskStartScheduler();
    while(true);
}

void pad_line(char *dest, const char *src, uint8_t len) {
    uint8_t i = 0;
    for (; i < len - 1 && src[i] != '\0'; ++i) {
        dest[i] = src[i];
    }
    for (; i < len - 1; ++i) {
        dest[i] = ' ';
    }
    dest[len - 1] = '\0'; // Ensure null-termination
}

void set_lcd_text(void *str) {
    char *text = (char *) str;
    char buf_1[MAX_CHARS + 1], buf_2[MAX_CHARS + 1];
    pad_line(buf_1, text, MAX_CHARS + 1);
    pad_line(buf_2, text + MAX_CHARS, MAX_CHARS + 1);

    lcd_set_cursor(0, 0);
    lcd_string(buf_1);

    lcd_set_cursor(1, 0);
    lcd_string(buf_2);
}


void setup_pwm(uint8_t gpio) {
    // Asigna función de PWM
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    // Configura frecuencia de PWM e inicializa
    uint32_t slice = pwm_gpio_to_slice_num(gpio);
    pwm_set_clkdiv(slice, 1.0f);
    pwm_set_wrap(slice, MAX_PWM_WRAP);
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
            input_data.increment = true;
        else
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
                    xQueueOverwrite(guard_data.queue, context->data);
                } else {
                    guard_data.callback(guard_data.param);
                }
                break;
            case I2C_RTC:
                if (guard_data.queue != NULL) {
                    guard_data.callback(guard_data.param);
                    xQueueOverwrite(guard_data.queue, (time_t *) guard_data.param);
                } else {
                    guard_data.callback(guard_data.param);
                }
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(SLEEP_I2C_GUARD));
    }
}

void task_ina219(void *pvParameters) {
    ina219_t ina219 = ina219_get_default_config();
    ina219.i2c = I2C_PORT;
    ina219.max_expected_amps = INA219_MAX_CURRENT;
    ina219.shunt_resistor_value = 0.1f;
    ina219.gain = INA219_GAIN_2_80MV;

    ina219_data_t ina219_data = {0};
    ina219_context_t context = {
        .data = &ina219_data,
        .ina219 = ina219
    };

    i2c_guard_t guard_data;
    guard_data = (i2c_guard_t) {
        .device = I2C_INA219,
        .queue = NULL,
        .callback = ina219_init_rtos,
        .param = (void *) &context
    };
    xQueueSend(
        queue_i2c_guard,
        &guard_data,
        portMAX_DELAY
    );

    guard_data.callback = ina219_get_data_rtos;
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


void task_rtc(void *pvParameters) {
    time_t current_time;
    i2c_guard_t guard_data = {
        .device = I2C_RTC,
        .queue = NULL,
        .callback = rtc_init,
        .param = (void *) I2C_PORT
    };
    xQueueSend(
        queue_i2c_guard,
        &guard_data,
        portMAX_DELAY
    );
    guard_data.queue = queue_rtc_time;
    guard_data.callback = rtc_get_time_rtos;
    guard_data.param = (void *) &current_time;

    while(1) {
        xQueueSend(
            queue_i2c_guard,
            &guard_data,
            portMAX_DELAY
        );
        vTaskDelay(pdMS_TO_TICKS(950));
    }
}