#include <string.h>
#include "pico/stdlib.h"

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
QueueHandle_t queue_sd_card;
QueueHandle_t queue_temp;
QueueHandle_t queue_uart_rx;

/* Mutex para proteger escritura en UART desde distintas tareas */
SemaphoreHandle_t uart_tx_mutex;

TaskHandle_t task_rtc_handle;
TaskHandle_t task_pid_handle = NULL;

uint8_t enable_index = 0;

datalogger_t buf_datalogger_1[LOGGER_CHUNK_SIZE] = {0};
datalogger_t buf_datalogger_2[LOGGER_CHUNK_SIZE] = {0};

system_config_t sys_conf = {
    .menu = MENU_MAIN,
    .index = 0,
    .fixed_index = false,
    .r_index = 1,
    .pwm_value = 0,
    .max_temp = MAX_TEMP,
    .max_current = MAX_CURRENT,
    .max_voltage = MAX_VOLTAGE
};

pid_config_t pid_conf = {
    .kp = 8.2f,
    .ki = 0.032f,
    .kd = 0.035f,
    .ki_limit = 5.0f,
    .kd_limit = 10.0f,
    .pid_time_ms = 0,
    .resistance_target = 300
};


void task_menu(void *pvParameters) {
    input_data_t input_data = {0};
    time_t edit_time = {0};
    uint16_t aux_wrap = 0;

    i2c_guard_t guard_data = {0};

    sd_event_t sd_event = {
        .type = CONFIG_FILE,
        .data = (void *) &sys_conf,
        .chunk_index = 1
    };
    while(1) {
        xQueueReceive(queue_input_data, &input_data, portMAX_DELAY);
        
        if (input_data.device == BTN_MENU) {
            sys_conf.menu = MENU_MAIN;
            sys_conf.index = 0;
            sys_conf.fixed_index = false;
            vTaskResume(task_rtc_handle);
            xSemaphoreGive(bin_btn_2);
            continue;
        }

        if (sys_conf.menu == MENU_MAIN) {
            if (input_data.device == ENCODER)
                wrap_index(&sys_conf.index, 5, input_data.increment);
            
            if (input_data.device == BTN_SWITCH) {
                switch (sys_conf.index) {
                    case MENU_TIME:
                        xQueuePeek(queue_rtc_time, &edit_time, portMAX_DELAY);
                        vTaskSuspend(task_rtc_handle);
                        sys_conf.menu = MENU_TIME;
                        break;
                    default:
                        sys_conf.menu = sys_conf.index;
                        sys_conf.index = 0;
                        break;
                }
                sys_conf.fixed_index = false;
                sys_conf.resistance_adj = pid_conf.resistance_target;
            }
            continue;
        }

        if (sys_conf.menu == MENU_SET_RESISTANCE) {
            if (input_data.device == ENCODER) {
                if (!sys_conf.fixed_index) {
                    wrap_index(&sys_conf.index, 4, input_data.increment);
                    continue;
                }
                switch (sys_conf.index) {
                    case 0:
                        if (input_data.increment)
                            sys_conf.resistance_adj += R_STEPS[sys_conf.r_index];
                        else
                            sys_conf.resistance_adj -= R_STEPS[sys_conf.r_index];

                        if (sys_conf.resistance_adj < MINIMUM_RESISTANCE) {
                            sys_conf.resistance_adj = MINIMUM_RESISTANCE;
                        } else if (sys_conf.resistance_adj > MAXIMUM_RESISTANCE) {
                            sys_conf.resistance_adj = MAXIMUM_RESISTANCE;
                        }
                        break;
                    case 1:
                        wrap_index(&sys_conf.r_index, 5, input_data.increment);
                        break;
                    case 2:
                        pid_conf.pid_time_ms += input_data.increment ? 500 : -500;

                        if (pid_conf.pid_time_ms < 0 || pid_conf.pid_time_ms > MAX_TIME_TARGET) {
                            pid_conf.pid_time_ms = 0;
                        }
                        break;
                }
            }

            if (input_data.device == BTN_SWITCH) {
                if (!sys_conf.fixed_index) {
                    if (sys_conf.index == 3) {
                        sys_conf.index = 0;
                        sys_conf.menu = MENU_PID;
                        pid_conf.resistance_target = sys_conf.resistance_adj;
                        sys_conf.fixed_index = false;
                        continue;
                    }
                } else {
                    xQueueSend(queue_sd_card, &sd_event, 0);
                }
                sys_conf.fixed_index = !sys_conf.fixed_index;
            }
            continue;
        }

        if (sys_conf.menu == MENU_PID) {
            if (input_data.device == ENCODER) {
                wrap_index(&sys_conf.index, 2, input_data.increment);
            }
            continue;
        }

        if (sys_conf.menu == MENU_TEST) {
            if (input_data.device == ENCODER) {
                if (input_data.increment) {
                    aux_wrap = sys_conf.pwm_value + 25;
                } else {
                    aux_wrap = sys_conf.pwm_value > 5 ? sys_conf.pwm_value - 5 : 0;
                }
                if (aux_wrap > MAX_PWM_DUTY) {
                    aux_wrap = MAX_PWM_DUTY;
                }
                if (aux_wrap < MIN_PWM_DUTY) {
                    gpio_put(PID_ENABLE_PIN, false);
                }
                sys_conf.pwm_value = aux_wrap;
                pwm_set_gpio_level(PWM_PIN, sys_conf.pwm_value);
                
            }
            continue;
        }

        if (sys_conf.menu == MENU_TIME) {
            if (input_data.device == ENCODER) {
                if (!sys_conf.fixed_index) {
                    // Cambiar campo a editar: 0=year, 1=month, 2=date, 3=hour, 4=minute, 5=second, 6=Guardar/Salir
                    sys_conf.index = (sys_conf.index + (input_data.increment ? 1 : 6)) % 7;
                } else {
                    // Editar el valor del campo seleccionado
                    switch (sys_conf.index) {
                        case 0: edit_time.year   += input_data.increment ? 1 : -1; break;
                        case 1: edit_time.month  = (edit_time.month  + (input_data.increment ? 0 : 10)) % 12 + 1; break;
                        case 2: edit_time.date   = (edit_time.date   + (input_data.increment ? 0 : 29)) % 31 + 1; break;
                        case 3: edit_time.hour   = (edit_time.hour   + (input_data.increment ? 1 : 23)) % 24; break;
                        case 4: edit_time.minute = (edit_time.minute + (input_data.increment ? 1 : 59)) % 60; break;
                        case 5: edit_time.second = (edit_time.second + (input_data.increment ? 1 : 59)) % 60; break;
                    }
                }
                xQueueOverwrite(queue_rtc_time, &edit_time);
                continue;
            }

            if (input_data.device == BTN_SWITCH) {
                if (!sys_conf.fixed_index) {
                    if (sys_conf.index == 6) {
                        // Guardar cambios en el RTC y volver al menú principal
                        guard_data = (i2c_guard_t) {
                            .device = I2C_RTC,
                            .queue = NULL,
                            .callback = rtc_set_time_rtos,
                            .param = (void *) &edit_time
                        };
                        xQueueSend(queue_i2c_guard, &guard_data, portMAX_DELAY);
                        sys_conf.menu = MENU_MAIN;
                        sys_conf.index = 0;
                        vTaskResume(task_rtc_handle);
                        continue;
                    }
                }
                // Entrar/salir de edición
                sys_conf.fixed_index = !sys_conf.fixed_index;
            }
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
    float temp_c;
    static bool blink_state = false;
    BaseType_t status;
    
    time_t current_time;

    static const char *main_options[] = {
        "Set Resistencia",
        "Control PID",
        "INA219 (TEST)",
        "Set Tiempo",
        "Menu SD"
    };
    const int menu_count = sizeof(main_options) / sizeof(main_options[0]);
    const uint8_t max_format_chars = MAX_CHARS + 1;
    
    while(1) {
        switch(sys_conf.menu) {
            case MENU_MAIN:
                // Calcula índice actual y el siguiente (scroll circular)
                int idx1 = sys_conf.index % menu_count;
                int idx2 = (sys_conf.index + 1) % menu_count;
                snprintf(line1, max_format_chars, "%c%s",
                    (sys_conf.index == idx1 & blink_state) ? '>' : ' ',
                    main_options[idx1]);
                snprintf(line2, max_format_chars, "%c%s",
                    (sys_conf.index == idx2 & blink_state) ? '>' : ' ',
                    main_options[idx2]);
                break;
            case MENU_SET_RESISTANCE:
                snprintf(line1, max_format_chars, "%cR:%4d%cPaso:%3d",
                    (sys_conf.index == 0 & blink_state) ? '>' : ' ',
                    sys_conf.resistance_adj,
                    (sys_conf.index == 1 & blink_state) ? '>' : ' ',
                    R_STEPS[sys_conf.r_index]
                );
                if (pid_conf.pid_time_ms == 0) {
                    snprintf(line2, max_format_chars, "%cT: u(t) %cSig.",
                        (sys_conf.index == 2 & blink_state) ? '>' : ' ',
                        (sys_conf.index == 3 & blink_state) ? '>' : ' '
                    );
                } else {
                    snprintf(line2, max_format_chars, "%cT:%4.1f s%cSig.",
                        (sys_conf.index == 2 & blink_state) ? '>' : ' ',
                        pid_conf.pid_time_ms / 1000.0f,
                        (sys_conf.index == 3 & blink_state) ? '>' : ' '
                    );
                }
                break;
            case MENU_PID:
                xQueuePeek(queue_ina219_data, &ina219_data, 1);
                xQueuePeek(queue_temp, &temp_c, 1);
                snprintf(line1, max_format_chars, "R:%4d|%4d|E:%c",
                    pid_conf.resistance_target,
                    (uint16_t) (ina219_data.voltage_v / ina219_data.current_a),
                    gpio_get(PID_ENABLE_PIN) ? 'X' : ' '
                );
                if (sys_conf.index == 0) {
                    snprintf(line2, max_format_chars, "W:%4d|I:%5.1fmA",
                        sys_conf.pwm_value,
                        1000.0f * ina219_data.current_a
                    );
                } else {
                    snprintf(line2, max_format_chars, "V:%4.2fV|T:%4.1fC",
                        ina219_data.voltage_v,
                        temp_c
                    );
                }
                break;
            case MENU_TEST:
                status = xQueuePeek(queue_ina219_data, &ina219_data, 1);
                if (status == pdFALSE) {
                    snprintf(line1, max_format_chars, "Error: INA219");
                    snprintf(line2, max_format_chars, "No data");
                } else {
                    snprintf(line1, max_format_chars, "W:%4d|V:%04.1fv", sys_conf.pwm_value, ina219_data.voltage_v);
                    snprintf(line2, max_format_chars, "C:%s |I:%04.2fmA", gpio_get(PID_ENABLE_PIN) ? "ON " : "OFF", ina219_data.current_a * 1000);
                }

                break;
            case MENU_TIME:
                BaseType_t queue_status = xQueuePeek(queue_rtc_time, &current_time, 1);
                if (queue_status == pdFALSE) {
                    snprintf(line1, max_format_chars, "Error: RTC");
                    snprintf(line2, max_format_chars, "No data");
                    break;
                }
                snprintf(line2, max_format_chars, "Time:%02d:%02d:%02d %s",
                    (sys_conf.index == 3 && !blink_state) ? 24 : current_time.hour,
                    (sys_conf.index == 4 && !blink_state) ? 60 : current_time.minute,
                    (sys_conf.index == 5 && !blink_state) ? 60 : current_time.second,
                    (sys_conf.index == 6 && !blink_state) ? " " : "Ok");
                snprintf(line1, max_format_chars, "Date:%04d-%02d-%02d",
                    (sys_conf.index == 0 && !blink_state) ? 9999 : current_time.year,
                    (sys_conf.index == 1 && !blink_state) ? 12 : current_time.month,
                    (sys_conf.index == 2 && !blink_state) ? 31 : current_time.date
                );
                break;

            case MENU_SD:
                if (sd_card_alive()) {
                    snprintf(line1, max_format_chars, "SD: Mounted");
                    snprintf(line2, max_format_chars, "Archivos: %d", sd_card_get_file_count());
                } else {
                    snprintf(line1, max_format_chars, "SD: Unmounted");
                    snprintf(line2, max_format_chars, "Inserte tarjeta");
                }
                break;

            case MENU_PROTECCION:
                snprintf(line1, max_format_chars, "Protection activada");
                if (sys_conf.index == 0) {
                    snprintf(line2, max_format_chars, "V >= %4.2fV", sys_conf.max_voltage);
                } else if (sys_conf.index == 1) {
                    snprintf(line2, max_format_chars, "I >= %5.1fmA", sys_conf.max_current);
                } else {
                    snprintf(line2, max_format_chars, "Temp >= %5.2fC", sys_conf.max_temp);
                }
                
                break;
                
            default:
                snprintf(line1, max_format_chars, "Menu: %d", sys_conf.menu);
                snprintf(line2, max_format_chars, "Index: %d", sys_conf.index);
                break;
        }
        xQueueSend(
            queue_i2c_guard,
            &guard_data,
            portMAX_DELAY
        );
        if (sys_conf.fixed_index)
            blink_state = true; // No parpadea si el índice es fijo
        else
            blink_state = !blink_state; // Alterna el estado de parpadeo

        vTaskDelay(pdMS_TO_TICKS(SLEEP_TIME_LCD));
    }
}

void task_pid_controller(void *pid_params) {
    datalogger_t *datalogger_ptr = buf_datalogger_1;
    sd_event_t sd_event = {
        .type = LOG_FILE,
        .data = (void *) datalogger_ptr
    };
    uint16_t datalogger_index = 0;
    time_t current_time;
    float temp;
    ina219_data_t ina219_data;

    pid_config_t params = *(pid_config_t *) pid_params;
    // Configuración del PWM
    uint16_t r_target = params.resistance_target;
    int16_t pendiente = 0;
    uint16_t target_steps = 0;
    uint16_t steps = 0;
    uint8_t stable_steps = 0;
    
    if (params.pid_time_ms > 0) {
        r_target = MAXIMUM_RESISTANCE;
        target_steps = params.pid_time_ms / CONTROLLER_REFRESH_MS;
        pendiente = (params.resistance_target - r_target) / target_steps;
    }
    
    float current_target_ma = 0;
    float error = 0;
    float last_error = 0.0f;
    float integral_value = 0;
    float derivative_value = 0;

    const float dt = CONTROLLER_REFRESH_MS / 1000.0f;
    
    TickType_t ticks = xTaskGetTickCount();
    
    while(1) {
        xQueuePeek(queue_temp, &temp, portMAX_DELAY);
        xQueuePeek(queue_ina219_data, &ina219_data, portMAX_DELAY);

        if (steps < target_steps) {
            r_target += pendiente;
            steps++;
        } else {
            r_target = pid_conf.resistance_target;
        }
        // if (r_target >= 1000) {
        //     kp = Kp * 2.0f;
        // } else if (r_target >= 350) {
        //     kp = Kp * 1.5f;
        // } else if (r_target >= 150) {
        //     kp = Kp * 1.0f;
        // } else if (r_target >= 60) {
        //     kp = Kp * 0.6f;
        // } else  {
        //     kp = Kp * 0.2f;
        // }
        current_target_ma = 1000.0f * ina219_data.voltage_v / (float) r_target;

        error = current_target_ma - (ina219_data.current_a * 1000.0f);
        if (fabsf(error / current_target_ma) <= 0.05f) {
            integral_value += error * dt / params.ki;
            derivative_value = 0.0f;
            stable_steps++;
        } else {
            if (last_error == 0)
                derivative_value = 0.0f;
            else 
                derivative_value = params.kd * (error - last_error) / dt;
            integral_value = 0.0f;
            stable_steps = 0;
        }

        limit_float(&integral_value, params.ki_limit);
        limit_float(&derivative_value, params.kd_limit);
        int16_t delta_duty = (int16_t) (
            pid_conf.kp * error +
            derivative_value +
            integral_value
        );
        last_error = error;
        if (sys_conf.pwm_value + delta_duty > MAX_PWM_DUTY) {
            sys_conf.pwm_value = MAX_PWM_DUTY;
        } else {
            sys_conf.pwm_value += delta_duty;
        }
        if (sys_conf.pwm_value < MIN_PWM_DUTY) sys_conf.pwm_value = MIN_PWM_DUTY;

        pwm_set_gpio_level(PWM_PIN, sys_conf.pwm_value);

        datalogger_ptr[datalogger_index] = (datalogger_t) {
            .voltage_v = ina219_data.voltage_v,
            .current_ma = ina219_data.current_a * 1000.0f,
            .pwm_value = sys_conf.pwm_value,
            .error = error,
            .integral = integral_value,
            .derivative = derivative_value,
            .r_target = r_target,
            .temperature = temp
        };
        if ((stable_steps > 10 && datalogger_index > LOGGER_MIN_SEND) || datalogger_index >= LOGGER_CHUNK_SIZE - 1) {
            sd_event.chunk_index = datalogger_index;
            if (xQueueSend(queue_sd_card, &sd_event, 0) == pdTRUE) {
                datalogger_index = 0;
                datalogger_ptr = datalogger_ptr == buf_datalogger_1 ? buf_datalogger_2 : buf_datalogger_1;
                sd_event.data = (void *) datalogger_ptr;
            }
        } else {
            datalogger_index++;
        }
        
        // vTaskDelayUntil(&ticks, pdMS_TO_TICKS(CONTROLLER_REFRESH_MS));
        vTaskDelay(pdMS_TO_TICKS(CONTROLLER_REFRESH_MS));
    }   
}

void task_protection(void * pvParameters) {
    ina219_data_t ina219_data;
    float temp = 25.0f;

    while(1) {
        xQueuePeek(queue_ina219_data, &ina219_data, portMAX_DELAY);
        // xQueuePeek(queue_temp, &temp, portMAX_DELAY);
        if (ina219_data.voltage_v >= sys_conf.max_voltage || ina219_data.current_a >= sys_conf.max_current || temp >= sys_conf.max_temp) {
            pwm_set_gpio_level(PWM_PIN, MIN_PWM_DUTY);
            gpio_put(PID_ENABLE_PIN, false);
            sys_conf.menu = MENU_PROTECCION;
            if (ina219_data.voltage_v >= sys_conf.max_voltage) {
                sys_conf.index = 0;
            } else if (ina219_data.current_a >= sys_conf.max_current) {
                sys_conf.index = 1;
            } else {
                sys_conf.index = 2;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void task_read_temp(void *pvParameters) {
    const float gain = 5.01f;
    const float convert_factor = 3.18f / (1 << 12); // Factor de conversión para 12 bits
    // const float slope_temp = (-20 - 140) / (0.6f - 0.245f);
    const float slope_temp = (-20 - 16) / (0.6f - 0.51f);
    float temp = 0.0f;
    // Configuracion de ADC
    adc_init();
    adc_gpio_init(26 + ADC_DIODE_TEMP);
    
    while(1) {
        float v_temp = adc_read() * convert_factor / gain;
        temp = v_temp * slope_temp + 220;
        xQueueOverwrite(queue_temp, &temp);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}


void task_datalogger(void *pvParameters) {
    static FATFS fs;
    static FRESULT f_res;
    static FIL fp;
    static UINT bw;

    static bool sd_mounted = false;

    sd_event_t sd_event = {0};
    char buffer[128] = {0};
    char filename[64] = {0};
    BaseType_t f_res_status;

    datalogger_t *data_to_log = NULL;
    time_t current_time = {0};
    uint16_t aux_read = 0;
    
    i2c_guard_t guard_data = {
        .device = I2C_RTC,
        .queue = NULL,
        .callback = rtc_get_time_rtos,
        .param = (void *) &current_time
    };
    
    while(1) {
        if (!USE_SERIAL_LOGGER && !sd_mounted) {
            f_res = f_mount(&fs, "", 1);
            if (f_res != FR_OK) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            f_res = f_open(&fp, "config.txt", FA_READ);
            if (f_res == FR_OK) {
                f_read(&fp, buffer, sizeof(buffer) - 1, &bw);
                buffer[bw] = '\0';
                char *token = strtok(buffer, ";");
                while (token != NULL) {
                    if (sscanf(token, "PID time: %d", &aux_read) == 1) {
                        pid_conf.pid_time_ms = aux_read;
                    }
                    else if (sscanf(token, "R Target: %d", &aux_read) == 1) {
                        if (aux_read >= MINIMUM_RESISTANCE && aux_read <= MAXIMUM_RESISTANCE) {
                            sys_conf.resistance_adj = aux_read;
                        }
                    }
                    else if (sscanf(token, "R Step index: %d", &aux_read) == 1) {
                        sys_conf.r_index = aux_read;
                    }
                    token = strtok(NULL, ";");
                }
                f_close(&fp);
            }
            sd_mounted = true;
        }

        xQueueReceive(queue_sd_card, &sd_event, portMAX_DELAY);
        #if USE_SERIAL_LOGGER
            if (sd_event.type != LOG_FILE) {
                continue; // Solo procesar eventos de tipo LOG_FILE
            }

            data_to_log = (datalogger_t *) sd_event.data;
            for (uint8_t i = 0; i < sd_event.chunk_index; i++) {
                printf("%.2f;%.2f;%04d;%.2f;%.2f;%.2f;%05d;%4.1f\n",
                    data_to_log[i].voltage_v,
                    data_to_log[i].current_ma,
                    data_to_log[i].pwm_value,
                    data_to_log[i].error,
                    data_to_log[i].integral,
                    data_to_log[i].derivative,
                    data_to_log[i].r_target,
                    data_to_log[i].temperature
                );
            }
        #else
        if (sd_event.type == LOG_FILE) {
            datalogger_t *data_to_log = (datalogger_t *) sd_event.data;

            xQueuePeek(queue_rtc_time, &current_time, portMAX_DELAY);
            snprintf(filename, sizeof(filename), "log-%04d_%02d_%02d-%03d_%02d__%.2f_%.2f_%.2f.csv",
                current_time.year, current_time.month, current_time.day,
                pid_conf.resistance_target, enable_index,
                pid_conf.kp, pid_conf.ki, pid_conf.kd
            );
            f_res = f_open(&fp, filename, FA_WRITE | FA_OPEN_APPEND);
            if (f_res != FR_OK) {
                sd_mounted = false;
                continue;
            }
            if (f_size(&fp) == 0) {
                f_res = f_write(&fp, file_header, strlen(file_header), &bw);
                if (f_res != FR_OK) {
                    f_close(&fp);
                    sd_mounted = false;
                    continue;
                }
            }
            // El puntero esta en la posicion final del archivo
            for (uint8_t i = 0; i < sd_event.chunk_index; i++) {
                snprintf(buffer, sizeof(buffer), "%.2f;%.2f;%04d;%.2f;%.2f;%.2f;%05d;%4.1f\n",
                    data_to_log[i].voltage_v,
                    data_to_log[i].current_ma,
                    data_to_log[i].pwm_value,
                    data_to_log[i].error,
                    data_to_log[i].integral,
                    data_to_log[i].derivative,
                    data_to_log[i].r_target,
                    data_to_log[i].temperature
                );
                f_res = f_write(&fp, buffer, strlen(buffer), &bw);
                if (f_res != FR_OK) {
                    // printf("Error writing to file: %d\n", f_res);
                    sd_mounted = false;
                    f_close(&fp);
                    continue;
                }
            }
            f_close(&fp);
        }
        else if (sd_event.type == CONFIG_FILE) {
            pid_config_t * config = (pid_config_t *) sd_event.data;
            f_res = f_open(&fp, "config.txt", FA_WRITE | FA_CREATE_ALWAYS);
            if (f_res != FR_OK) {
                sd_mounted = false;
                continue;
            }
            snprintf(buffer, sizeof(buffer), "PID time: %05d;R Target: %d;Kp: %.2f;Ki: %.2f;Kd: %.2f\n",
                config->pid_time_ms,
                config->resistance_target,
                config->kp,
                config->ki,
                config->kd
            );
            f_res = f_write(&fp, buffer, strlen(buffer), &bw);
            if (f_res != FR_OK) {
                printf("Error writing to file: %d\n", f_res);
            }
            f_close(&fp);
        }
        #endif
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
    queue_sd_card = xQueueCreate(3, sizeof(sd_event_t));
    if (queue_sd_card == NULL) {
        printf("Error al crear la cola de datalogger\n");
        return -1;
    }
    queue_temp = xQueueCreate(1, sizeof(float));
    if (queue_temp == NULL) {
        printf("Error al crear la cola de temperatura\n");
        return -1;
    }
    // Cola y mutex para manejo de UART
    queue_uart_rx = xQueueCreate(RX_BUFFER_SIZE, sizeof(char));
    if (queue_uart_rx == NULL) {
        printf("Error al crear la cola UART RX\n");
        return -1;
    }

    uart_tx_mutex = xSemaphoreCreateMutex();
    if (uart_tx_mutex == NULL) {
        printf("Error al crear el mutex UART TX\n");
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

    rtc_set_i2c(I2C_PORT);

    // Init PWM y ENABLE PID
    setup_pwm(PWM_PIN);
    gpio_init(PID_ENABLE_PIN);
    gpio_set_dir(PID_ENABLE_PIN, GPIO_OUT);
    gpio_put(PID_ENABLE_PIN, 0);

    
    setup_uart();

    // Creacion de tareas
    xTaskCreate(
        task_btn_stop_pull_up, "task_btn_stop", configMINIMAL_STACK_SIZE * 1,
        &btn_data_2, 4, NULL
    );
    xTaskCreate(
        task_protection, "task_protection", configMINIMAL_STACK_SIZE * 2,
        NULL, 4, NULL
    );
    // xTaskCreate(
    //     task_pid_controller, "task_pid_controller", configMINIMAL_STACK_SIZE * 2,
    //     NULL, 4, NULL
    // );
    xTaskCreate(
        task_i2c_guard, "task_i2c_guard", configMINIMAL_STACK_SIZE * 3,
        NULL, 4, NULL
    );
    xTaskCreate(
        task_ina219, "task_ina219", configMINIMAL_STACK_SIZE * 1,
        NULL, 3, NULL
    );
    xTaskCreate(
        task_menu, "task_menu", configMINIMAL_STACK_SIZE * 3,
        NULL, 2, NULL
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
        task_datalogger, "task_datalogger", configMINIMAL_STACK_SIZE * 5,
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
    xTaskCreate(task_read_temp, "task_read_temp", configMINIMAL_STACK_SIZE * 1,
        NULL, 1, NULL
    );
    xTaskCreate(task_rtc, "task_rtc", configMINIMAL_STACK_SIZE * 1,
        NULL, 1, &task_rtc_handle
    );
    xTaskCreate(task_uart, "task_usart", configMINIMAL_STACK_SIZE * 5,
        NULL, 1, NULL
    );
    vTaskStartScheduler();
    while(true);
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


void task_btn_stop_pull_up(void *pvParameters) {
    btn_data_t * btn_data = (btn_data_t *) pvParameters;
    // bool pid_running = false;
    gpio_init(btn_data->gpio);
    gpio_set_dir(btn_data->gpio, GPIO_IN);
    gpio_pull_up(btn_data->gpio);
    gpio_set_irq_enabled_with_callback(btn_data->gpio, GPIO_IRQ_EDGE_RISE, false, btn_irq_handler);
    gpio_set_irq_enabled_with_callback(btn_data->gpio, GPIO_IRQ_EDGE_FALL, true, btn_irq_handler);

    while(1) {
        xSemaphoreTake(*btn_data->sem_bin, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME));
        // Accion al presionar el boton
        sys_conf.pwm_value = MIN_PWM_DUTY;
        if (!gpio_get(PID_ENABLE_PIN) && (sys_conf.menu == MENU_PID || sys_conf.menu == MENU_TEST)) {
            gpio_put(PID_ENABLE_PIN, true);
            // if( sys_conf.menu == MENU_PID ) {
            //     xTaskCreate(
            //         task_pid_controller, "task_pid_controller", configMINIMAL_STACK_SIZE * 2,
            //         (void *)&pid_conf, 4, &task_pid_handle
            //     );
            // }
        } else {
            gpio_put(PID_ENABLE_PIN, false);
            pwm_set_gpio_level(PWM_PIN, sys_conf.pwm_value);
            if (task_pid_handle != NULL) {
                vTaskDelete(task_pid_handle);
                task_pid_handle = NULL;
            }
        }
        // Espera a que se suelte el boton
        if (!gpio_get(btn_data->gpio)) {
            gpio_set_irq_enabled(btn_data->gpio, GPIO_IRQ_EDGE_RISE, true);
            xSemaphoreTake(*btn_data->sem_bin, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME));
        }
        gpio_set_irq_enabled(btn_data->gpio, GPIO_IRQ_EDGE_FALL, true);
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
        printf("Button %d pressed\n", btn_data->device);

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
                    ina219_context_t *context = (ina219_context_t *) guard_data.param;
                    guard_data.callback(guard_data.param);
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
        .queue = queue_rtc_time,
        .callback = rtc_get_time_rtos,
        .param = (void *) &current_time
    };
    rtc_set_i2c(I2C_PORT);

    while(1) {
        xQueueSend(
            queue_i2c_guard,
            &guard_data,
            portMAX_DELAY
        );
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void limit_float(float *value, float max) {
    if (*value > max) {
        *value = max;
    } else if (*value < -max) {
        *value = -max;
    }
}

void wrap_index(uint8_t *index, uint8_t max_index, bool increment) {
    if (increment)
        *index = (*index + 1) % max_index;
    else
        *index = (*index + max_index - 1) % max_index;
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



void uart_irq_handler() {
    static BaseType_t to_high_priority = pdFALSE;
    static char c;
    // Leer todos los bytes disponibles
    while (uart_is_readable(UART_ID)) {
        c = uart_getc(UART_ID);
        xQueueSendFromISR(queue_uart_rx, &c, &to_high_priority);
    }
    portYIELD_FROM_ISR(to_high_priority);
}

void setup_uart() {
    int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;
    // Configurar la UART
    uart_init(UART_ID, UART_BAUD_RATE);
    gpio_set_function(UART_TX_PIN, UART_FUNCSEL_NUM(UART_ID, UART_TX_PIN));
    gpio_set_function(UART_RX_PIN, UART_FUNCSEL_NUM(UART_ID, UART_RX_PIN));
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);

    // Habilitar interrupciones de la UART (la ISR encola líneas completas)
    irq_set_exclusive_handler(UART_IRQ, uart_irq_handler);
    irq_set_enabled(UART_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);
}

void task_uart(void *pvParameters) {
    // Buffer temporal para recibir comandos desde la cola
    char cmd_buf[RX_BUFFER_SIZE];
    uint8_t cmd_idx = 0;
    char uart_tx_buf[RX_BUFFER_SIZE];
    char *token = NULL;

    uint8_t command;
    uint8_t var;
    float aux_f;
    uint16_t aux_i;

    ina219_data_t ina219_data;
    float temp = 25.0f;

    char c;

    while (1) {
        xQueueReceive(queue_uart_rx, &c, portMAX_DELAY);
        if (cmd_idx == 0 && c != '$') {
            continue; // Ignorar hasta encontrar el inicio del comando
        }
        cmd_buf[cmd_idx++] = c;
        if (cmd_buf[cmd_idx - 1] == '\n' || cmd_buf[cmd_idx - 1] == '\r' || cmd_idx >= RX_BUFFER_SIZE) {
            cmd_buf[cmd_idx - 1] = '\0'; // Termina la cadena
            cmd_idx = 0;
            // Proteger escrituras por UART (respuesta/ack) con el mutex
            if (xSemaphoreTake(uart_tx_mutex, portMAX_DELAY) == pdTRUE) {
                token = strtok(cmd_buf, " ");
                if (strcmp(token, "$set") == 0) {
                    command = CMD_SET;
                } else if (strcmp(token, "$get") == 0) {
                    command = CMD_GET;
                } else if(strcmp(token, "$echo") == 0) {
                    printf("Comando ECHO recibido\n");
                    command = CMD_ECHO;
                    uart_puts(UART_ID, "TX: ");
                    uart_puts(UART_ID, cmd_buf + 6);
                    xSemaphoreGive(uart_tx_mutex);
                    continue;
                } else {
                    uart_puts(UART_ID, "Error: Comando no reconocido\n");
                    xSemaphoreGive(uart_tx_mutex);
                    continue;
                }
                token = strtok(NULL, " \n\r\0");
                for (int i = 0; i < sizeof(var_map) / sizeof(var_map[0]); i++) {
                    if (strcmp(token, var_map[i].cmd) == 0) {
                        var = var_map[i].var;
                        break;
                    }
                }

                if (command == CMD_SET) {
                    token = strtok(NULL, " \n\r\0");
                    switch (var) {
                        case VAR_KP:
                            aux_f = atof(token);
                            if (aux_f <= 0) {
                                uart_puts(UART_ID, "Error set: Kp no puede ser negativo o 0\n");
                                xSemaphoreGive(uart_tx_mutex);
                                continue;
                            }
                            pid_conf.kp = aux_f;
                            limit_float(&pid_conf.kp, MAX_KP);
                            break;
                        case VAR_KI:
                            aux_f = atof(token);
                            if (aux_f <= 0) {
                                uart_puts(UART_ID, "Error set: Ki no puede ser negativo o 0\n");
                                xSemaphoreGive(uart_tx_mutex);
                                continue;
                            }
                            pid_conf.ki = aux_f;
                            limit_float(&pid_conf.ki, MAX_KI);
                            break;
                        case VAR_KD:
                            aux_f = atof(token);
                            if (aux_f <= 0) {
                                uart_puts(UART_ID, "Error set: Kd no puede ser negativo o 0\n");
                                xSemaphoreGive(uart_tx_mutex);
                                continue;
                            }
                            pid_conf.kd = aux_f;
                            limit_float(&pid_conf.kd, MAX_KD);
                            break;
                        case VAR_R_TARGET:
                            aux_i = atoi(token);
                            if (aux_i < MINIMUM_RESISTANCE || aux_i > MAXIMUM_RESISTANCE) {
                                uart_puts(UART_ID, "Error set: R Target fuera de rango\n");
                                xSemaphoreGive(uart_tx_mutex);
                                continue;
                            }
                            else
                                pid_conf.resistance_target = aux_i;
                            break;
                        case VAR_TIME_TARGET:
                            aux_i = atoi(token);
                            if (aux_i < 0 || aux_i > MAX_TIME_TARGET) {
                                uart_puts(UART_ID, "Error set: PID Time invalido\n");
                                xSemaphoreGive(uart_tx_mutex);
                                continue;
                            }
                            else
                                pid_conf.pid_time_ms = aux_i;
                            break;
                        case VAR_KI_LIM:
                            aux_f = atof(token);
                            if (aux_f < 0 || aux_f > MAX_INTEGRAL_VALUE) {
                                uart_puts(UART_ID, "Error set: Ki Limit invalido\n");
                                xSemaphoreGive(uart_tx_mutex);
                                continue;
                            }
                            pid_conf.ki_limit = (float) aux_f;
                            break;
                        case VAR_KD_LIM:
                            aux_f = atof(token);
                            if (aux_f < 0 || aux_f > MAX_DERIVATIVE_VALUE) {
                                uart_puts(UART_ID, "Error set: Kd Limit invalido\n");
                                xSemaphoreGive(uart_tx_mutex);
                                continue;
                            }
                            pid_conf.kd_limit = (float) aux_f;
                            break;
                        case VAR_MAX_TEMP:
                            aux_f = atof(token);
                            if (aux_f < MIN_TEMP || aux_f > MAX_TEMP) {
                                uart_puts(UART_ID, "Error set: Max Temp invalido\n");
                                xSemaphoreGive(uart_tx_mutex);
                                continue;
                            }
                            sys_conf.max_temp = aux_f;
                            break;
                        
                        case VAR_MAX_VOLTAGE:
                            aux_f = atof(token);
                            if (aux_f < MIN_VOLTAGE ||  aux_f > MAX_VOLTAGE) {
                                uart_puts(UART_ID, "Error set: Max Voltage invalido\n");
                                xSemaphoreGive(uart_tx_mutex);
                                continue;
                            }
                            sys_conf.max_voltage = aux_f;
                            break;
                        case VAR_MAX_CURRENT:
                            aux_f = atof(token);
                            if (aux_f < MIN_CURRENT || aux_f > MAX_CURRENT) {
                                uart_puts(UART_ID, "Error set: Max Current invalido\n");
                                xSemaphoreGive(uart_tx_mutex);
                                continue;
                            }
                            sys_conf.max_current = aux_f;
                            break;
                        default:
                            uart_puts(UART_ID, "Error set: Variable no reconocida\n");
                            break;
                    }
                }
                switch (var) {
                    case VAR_KP:
                        sprintf(uart_tx_buf, "Kp: %.2f\n", pid_conf.kp);
                        uart_puts(UART_ID, uart_tx_buf);
                        break;
                    case VAR_KI:
                        sprintf(uart_tx_buf, "Ki: %.2f\n", pid_conf.ki);
                        uart_puts(UART_ID, uart_tx_buf);
                        break;
                    case VAR_KD:
                        sprintf(uart_tx_buf, "Kd: %.2f\n", pid_conf.kd);
                        uart_puts(UART_ID, uart_tx_buf);
                        break;
                    case VAR_R_TARGET:
                        sprintf(uart_tx_buf, "R_Target: %d\n", pid_conf.resistance_target);
                        uart_puts(UART_ID, uart_tx_buf);
                        break;
                    case VAR_TIME_TARGET:
                        sprintf(uart_tx_buf, "PID_Time: %d\n", pid_conf.pid_time_ms);
                        uart_puts(UART_ID, uart_tx_buf);
                        break;
                    case VAR_KI_LIM:
                        sprintf(uart_tx_buf, "Ki_Limit: %.2f\n", pid_conf.ki_limit);
                        uart_puts(UART_ID, uart_tx_buf);
                        break;
                    case VAR_KD_LIM:
                        sprintf(uart_tx_buf, "Kd_Limit: %.2f\n", pid_conf.kd_limit);
                        uart_puts(UART_ID, uart_tx_buf);
                        break;
                    case VAR_MAX_TEMP:
                        sprintf(uart_tx_buf, "Max_Temp: %.2f\n", sys_conf.max_temp);
                        uart_puts(UART_ID, uart_tx_buf);
                        break;
                    case VAR_MAX_VOLTAGE:
                        sprintf(uart_tx_buf, "Max_Voltage: %.2f\n", sys_conf.max_voltage);
                        uart_puts(UART_ID, uart_tx_buf);
                        break;
                    case VAR_MAX_CURRENT:
                        sprintf(uart_tx_buf, "Max_Current: %.2f\n", sys_conf.max_current);
                        uart_puts(UART_ID, uart_tx_buf);
                        break;
                    case GET_VOLT:
                        xQueuePeek(queue_ina219_data, &ina219_data, portMAX_DELAY);
                        sprintf(uart_tx_buf, "Voltage: %.2f V\n", ina219_data.voltage_v);
                        uart_puts(UART_ID, uart_tx_buf);
                        break;
                    case GET_CURRENT:
                        xQueuePeek(queue_ina219_data, &ina219_data, portMAX_DELAY);
                        sprintf(uart_tx_buf, "Current: %.2f mA\n", ina219_data.current_a * 1000);
                        uart_puts(UART_ID, uart_tx_buf);
                        break;
                    case GET_TEMP:
                        xQueuePeek(queue_temp, &temp, portMAX_DELAY);
                        sprintf(uart_tx_buf, "Temperature: %.2f C\n", temp);
                        uart_puts(UART_ID, uart_tx_buf);
                        break;
                    case GET_SD:
                        sprintf(uart_tx_buf, "SD_Card: %s\n", USE_SERIAL_LOGGER ? "Serial Logger" : "Datalogger");
                        uart_puts(UART_ID, uart_tx_buf);
                        break;
                    // case GET_STATUS:
                    // uart_puts(UART_ID, "Status: running\n");
                    // break;
                    case GET_PROTECTION:
                        if (sys_conf.menu != MENU_PROTECCION) {
                            uart_puts(UART_ID, "Protection: running\n");
                        } else {
                            switch (sys_conf.index) {
                                case OVER_VOLTAGE:
                                    uart_puts(UART_ID, "Protection: Over Voltage\n");
                                    break;
                                case OVER_CURRENT:
                                    uart_puts(UART_ID, "Protection: Over Current\n");
                                    break;
                                case OVER_TEMPERATURE:
                                    uart_puts(UART_ID, "Protection: Over Temperature\n");
                                    break;
                            }
                        }
                        break;
                    default:
                        uart_puts(UART_ID, "Error: Variable no reconocida\n");
                        break;
                }

                xSemaphoreGive(uart_tx_mutex);
            }
        }
    }
}
