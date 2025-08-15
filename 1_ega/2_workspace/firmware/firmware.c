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

uint8_t enable_index = 0;
uint8_t resistance_step = 50;

system_config_t system_config = {
    .menu = MENU_MAIN,
    .index = 0,
    .fixed_index = false,
    .pid_enabled = false,
    .pwm_value = PWM_OFF,
    .pid_time_ms = 0,
    .resistance_target = 300,
    .sd_mounted = false,
    .sd_file_count = 0
};

void task_menu(void *pvParameters) {
    input_data_t input_data = {0};
    time_t edit_time = {0};
    uint8_t resistance_step_index = 1;
    uint8_t resistance_steps[] = {1, 10, 50, 100, 250};
    i2c_guard_t guard_data = {0};

    while(1) {
        xQueueReceive(queue_input_data, &input_data, portMAX_DELAY);

        if (input_data.device == BTN_STOP) {
            if (system_config.menu == MENU_PID_TUNING || system_config.menu == MENU_TEST) {
                system_config.pid_enabled = !system_config.pid_enabled;
                if (system_config.pid_enabled)
                    enable_index++;
            } else {
                system_config.pid_enabled = false;
            }
            continue;
        }
        
        if (input_data.device == BTN_MENU) {
            system_config.menu = MENU_MAIN;
            system_config.index = 0;
            system_config.fixed_index = false;
            system_config.pid_enabled = false;
            continue;
        }
        
        switch (system_config.menu) {
            case MENU_MAIN:
                if (input_data.device == ENCODER) {
                    wrap_index(&system_config.index, 6, input_data.increment);
                }
                else if (input_data.device == BTN_SWITCH) {
                    switch (system_config.index) {
                        case MENU_TIME:
                            guard_data = (i2c_guard_t) {
                                .device = I2C_RTC,
                                .queue = queue_rtc_time,
                                .callback = rtc_get_time_rtos,
                                .param = (void *) &edit_time
                            };
                            xQueueSend(queue_i2c_guard, &guard_data, portMAX_DELAY);
                            break;
                        case MENU_SD:
                            system_config.sd_mounted = sd_card_alive();
                            if (system_config.sd_mounted) {
                                system_config.sd_file_count = sd_card_get_file_count();
                            }
                            break;
                    }
                    system_config.menu = system_config.index;
                    system_config.index = 0;
                    system_config.pid_enabled = false;
                    system_config.resistance_adj = system_config.resistance_target;
                }
            break;
            case MENU_SET_RESISTANCE:
                if (input_data.device == ENCODER) {
                    if (!system_config.fixed_index) {
                        wrap_index(&system_config.index, 4, input_data.increment);
                        continue;
                    }
                    switch (system_config.index) {
                        case 0:
                            if (system_config.resistance_adj < (resistance_step + MINIMUM_RESISTANCE) && !input_data.increment) {
                                system_config.resistance_adj = MINIMUM_RESISTANCE;
                            } else {
                                system_config.resistance_adj += input_data.increment ? resistance_step : -resistance_step;
                            }
                            break;
                        case 1:
                            wrap_index(&resistance_step_index, 5, input_data.increment);
                            resistance_step = resistance_steps[resistance_step_index];
                            break;
                        case 2:
                            system_config.pid_time_ms += input_data.increment ? 100 : -100;
                            if (system_config.pid_time_ms < 0 || system_config.pid_time_ms > 50000) {
                                system_config.pid_time_ms = 0;
                            }
                            break;
                    }
                }

                if (input_data.device == BTN_SWITCH) {
                    if (!system_config.fixed_index && system_config.index == 3) {
                        system_config.index = 0;
                        system_config.menu = MENU_PID_TUNING;
                        system_config.resistance_target = system_config.resistance_adj;
                        system_config.fixed_index = false;
                    } else {
                        system_config.fixed_index = !system_config.fixed_index;
                    }
                }
            break;
            case MENU_TEST:
                if (input_data.device == ENCODER) {
                    int16_t aux_wrap = (system_config.pwm_value + (input_data.increment ? 50 : -5));
                    if (aux_wrap > MAX_PWM_DUTY) {
                        aux_wrap = MAX_PWM_DUTY;
                    }
                    else if (aux_wrap < PWM_OFF) {
                        aux_wrap = PWM_OFF;
                    }
                    system_config.pwm_value = aux_wrap;
                }
            break;
            case MENU_TIME:
                if (input_data.device == ENCODER) {
                    if (!system_config.fixed_index) {
                        // Cambiar campo a editar: 0=year, 1=month, 2=date, 3=hour, 4=minute, 5=second, 6=Guardar/Salir
                        wrap_index(&system_config.index, 7, input_data.increment);
                    } else {
                        // Editar el valor del campo seleccionado
                        switch (system_config.index) {
                            case 0: edit_time.year   += input_data.increment ? 1 : -1; break;
                            case 1: edit_time.month  = (edit_time.month  + (input_data.increment ? 1 : 11)) % 12 + 1; break;
                            case 2: edit_time.date   = (edit_time.date   + (input_data.increment ? 1 : 30)) % 31 + 1; break;
                            case 3: edit_time.hour   = (edit_time.hour   + (input_data.increment ? 1 : 23)) % 24; break;
                            case 4: edit_time.minute = (edit_time.minute + (input_data.increment ? 1 : 59)) % 60; break;
                            case 5: edit_time.second = (edit_time.second + (input_data.increment ? 1 : 59)) % 60; break;
                        }
                    }
                    xQueueOverwrite(queue_rtc_time, &edit_time);
                    continue;
                }
    
                if (input_data.device == BTN_SWITCH) {
                    if (!system_config.fixed_index) {
                        if (system_config.index == 6) {
                            // Guardar cambios en el RTC y volver al menú principal
                            guard_data = (i2c_guard_t) {
                                .device = I2C_RTC,
                                .queue = NULL,
                                .callback = rtc_set_time_rtos,
                                .param = (void *) &edit_time
                            };
                            xQueueSend(queue_i2c_guard, &guard_data, portMAX_DELAY);
                            system_config.menu = MENU_MAIN;
                            system_config.index = 0;
                            continue;
                        }
                    }
                    // Entrar/salir de edición
                    system_config.fixed_index = !system_config.fixed_index;
                }
            break;
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
    BaseType_t status;
    
    time_t current_time;

    static const char *main_options[] = {
        "Set Resistencia",
        "Control PID",
        "Reg Fuente",
        "INA219 (TEST)",
        "Set Tiempo",
        "Menu SD"
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
                snprintf(line1, max_format_chars, "%cR:%4d%cPaso:%3d",
                    (system_config.index == 0 & blink_state) ? '>' : ' ',
                    system_config.resistance_adj,
                    (system_config.index == 1 & blink_state) ? '>' : ' ',
                    resistance_step
                );
                if (system_config.pid_time_ms == 0) {
                    snprintf(line2, max_format_chars, "%cT: u(t) %cSig.",
                        (system_config.index == 2 & blink_state) ? '>' : ' ',
                        (system_config.index == 3 & blink_state) ? '>' : ' '
                    );
                } else {
                    snprintf(line2, max_format_chars, "%cT:%4.1f s%cSig.",
                        (system_config.index == 2 & blink_state) ? '>' : ' ',
                        system_config.pid_time_ms / 1000.0f,
                        (system_config.index == 3 & blink_state) ? '>' : ' '
                    );
                }
                break;
            case MENU_PID_TUNING:
                xQueuePeek(queue_ina219_data, &ina219_data, 1);

                snprintf(line1, max_format_chars, "R:%4d|%4d|E:%c",
                    system_config.resistance_target,
                    (uint16_t) (1000.0f * ina219_data.voltage_v / ina219_data.current_ma),
                    system_config.pid_enabled ? 'X' : ' '
                );
                snprintf(line2, max_format_chars, "W:%4d|I:%5.1fmA",
                    system_config.pwm_value - MIN_PWM_DUTY,
                    ina219_data.current_ma
                );
                break;
            case MENU_TEST:
                status = xQueuePeek(queue_ina219_data, &ina219_data, 1);
                if (status == pdFALSE) {
                    snprintf(line1, max_format_chars, "Error: INA219");
                    snprintf(line2, max_format_chars, "No data");
                } else {
                    snprintf(line1, max_format_chars, "W:%5d|V:%04.1fv", system_config.pwm_value, ina219_data.voltage_v);
                    snprintf(line2, max_format_chars, "C:%s |I:%04.2fmA", system_config.pid_enabled ? "ON " : "OFF", ina219_data.current_ma);
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
                    (system_config.index == 3 && !blink_state) ? 24 : current_time.hour,
                    (system_config.index == 4 && !blink_state) ? 60 : current_time.minute,
                    (system_config.index == 5 && !blink_state) ? 60 : current_time.second,
                    (system_config.index == 6 && !blink_state) ? " " : "Ok");
                snprintf(line1, max_format_chars, "Date:%04d-%02d-%02d",
                    (system_config.index == 0 && !blink_state) ? 9999 : current_time.year,
                    (system_config.index == 1 && !blink_state) ? 12 : current_time.month,
                    (system_config.index == 2 && !blink_state) ? 31 : current_time.date
                );
                break;

            case MENU_SD:
                snprintf(line1, max_format_chars, "SD: %s", system_config.sd_mounted ? "Mounted" : "Unmounted");
                snprintf(line2, max_format_chars, "Index: %d", system_config.index);
                break;
            case MENU_PROTECTION:

                if (system_config.index == 0) {
                    snprintf(line1, max_format_chars, "Protection: Voltage");
                    sniprintf(line2, max_format_chars, "Value: %.2f V", ina219_data.voltage_v);
                }
                else if (system_config.index == 1) {
                    snprintf(line1, max_format_chars, "Protection: Current");
                    snprintf(line2, max_format_chars, "Value: %.2f mA", ina219_data.current_ma);
                }
                else {
                    snprintf(line1, max_format_chars, "Error: Unknown Protection");
                    snprintf(line2, max_format_chars, "Index: %d", system_config.index);
                }
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
    uint16_t pwm_value = 0;
    float current_target_ma = 0;
    float error = 0;
    float last_error = 0;
    float integral_value = 0;
    float derivative_value = 0;
    float kp = Kp;
    float ki = Ki;
    float kd = Kd;
    float med_current_ma = 0.6f;
    const float alpha_current = 0.75f; // Filtro exponencial para suavizar la corriente
    const float dt = CONTROLLER_REFRESH_MS / 1000.0f;

    sd_event_t sd_event = {
        .type = LOG_FILE,
        .data = {0}
    };

    bool reset_enable = true;
    uint16_t steps_idx = 0;
    uint16_t steps = 0;
    int16_t pendiente = 0;
    uint16_t r_target = system_config.resistance_target;

    TickType_t ticks = xTaskGetTickCount();
    
    while(1) {
        
        gpio_put(PID_STATUS_PIN, system_config.pid_enabled);
        if (!system_config.pid_enabled) {
            system_config.pwm_value = PWM_OFF;
            integral_value = 0.0f;
            derivative_value = 0.0f;
            last_error = 0.0f;
            reset_enable = true;
            med_current_ma = 0.6f;
            pwm_set_gpio_level(PWM_PIN, system_config.pwm_value);
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        vTaskDelayUntil(&ticks, pdMS_TO_TICKS(CONTROLLER_REFRESH_MS));
        
        xQueuePeek(queue_ina219_data, &ina219_data, portMAX_DELAY);
        if (ina219_data.current_ma < 0.1f) {
            med_current_ma = 0.0f;
        } else {
            med_current_ma = alpha_current * ina219_data.current_ma + (1 - alpha_current) * med_current_ma;
        }
        if (ina219_data.voltage_v >= MAX_VOLTAGE || ina219_data.current_ma >= MAX_CURRENT) {
            // printf("Error: Voltage or current out of range, Disabling PID calculation.\n");
            system_config.pid_enabled = false;
            pwm_set_gpio_level(PWM_PIN, PWM_OFF);
            system_config.menu = MENU_PROTECTION;
            system_config.index = (ina219_data.voltage_v >= MAX_VOLTAGE) ? 0 : 1;
            continue;
        }
        if (system_config.menu == MENU_PID_TUNING) {
            if (ina219_data.current_ma < 0.9f) {
                // printf("Error: Current is zero, skipping PID calculation.\n");
                if (pwm_value < MIN_PWM_DUTY) {
                    pwm_value = MIN_PWM_DUTY;
                } else {
                    pwm_value += 1;
                }
                pwm_set_gpio_level(PWM_PIN, pwm_value);
                continue;
            }

            if (reset_enable) {
                if (system_config.pid_time_ms > 0) {
                    r_target = (uint16_t) (1000.0f * ina219_data.voltage_v / med_current_ma);
                    steps = system_config.pid_time_ms / CONTROLLER_REFRESH_MS;
                    pendiente = (system_config.resistance_target - r_target) / steps; // ohms por cada ciclo
                    // printf("Steps: %d, Pendiente: %d, R_target: %d\n", steps, pendiente, r_target);
                } else {
                    r_target = system_config.resistance_target;
                    pendiente = 0;
                    steps = 0;
                }
                steps_idx = 0;
                reset_enable = false;
            }
            if (steps_idx < steps) {
                r_target += pendiente; // Incrementar el objetivo de resistencia
            } else {
                r_target = system_config.resistance_target; // Mantener el objetivo de resistencia
            }
            
            if (r_target >= 200) {
                kp = Kp;
            } else if (r_target >= 130) {
                kp = Kp * 0.8f;
            } else if (r_target >= 60) {
                kp = Kp * 0.4f;
            } else  {
                kp = Kp * 0.15f;
            }

            current_target_ma = 1000.0f * ina219_data.voltage_v / r_target;
            error = current_target_ma - med_current_ma;

            // Seleccionar control PI o PD
            if (fabsf(error) <= 3.0f) {
                integral_value += error * dt;
                derivative_value = 0.0f;
            } else {
                derivative_value = (error - last_error) / dt;
                integral_value = 0.0f;
            }
            // Limitar valores para evitar wind-up y picos excesivos
            limit_float(&integral_value, MAX_INTEGRAL_VALUE);
            limit_float(&derivative_value, MAX_DERIVATIVE_VALUE);
            
            int16_t delta_duty = (int16_t) (
                kp * error +
                kd * derivative_value +
                ki * integral_value
            );
            last_error = error;
            pwm_value += delta_duty;
        
            if (pwm_value < MIN_PWM_DUTY) pwm_value = MIN_PWM_DUTY;

            if (pwm_value > MAX_PWM_DUTY) pwm_value = MAX_PWM_DUTY;

            system_config.pwm_value = pwm_value;
            pwm_set_gpio_level(PWM_PIN, pwm_value);
        } else if (system_config.menu == MENU_TEST) {
            if (system_config.pwm_value >= MIN_PWM_DUTY && system_config.pwm_value <= MAX_PWM_DUTY) {
                pwm_set_gpio_level(PWM_PIN, system_config.pwm_value);
                gpio_put(PID_STATUS_PIN, true);
                pwm_value = system_config.pwm_value;
            }
        }
        
        if (steps_idx % LOGGER_ITER_FOR_LOG == 0) {
            if (steps_idx == 0) {
                printf("Voltage;Current;Error;PWM Value;Integral;Derivative;R_Target\n");
            }
            // sd_event.data = (datalogger_t) {
            //     .temperature = 50.0f,
            //     .current_ma = med_current_ma,
            //     .voltage_v = ina219_data.voltage_v,
            //     .error = error,
            //     .pwm_value = pwm_value,
            //     .integral = integral_value,
            //     .derivative = derivative_value,
            //     .r_target = r_target
            // };
            // BaseType_t status = xQueueSendToBack(queue_datalogger, &sd_event, 1);
            printf("%.2f;%.2f;%.2f;%05d;%.2f;%.2f;%04d\n",
                ina219_data.voltage_v,
                med_current_ma,
                error,
                pwm_value,
                integral_value,
                derivative_value,
                r_target
            );
        }
        steps_idx++;
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
        float v_temp = adc_read() * convert_factor / 4.92f;
        float temp = (v_temp - 0.245f) / slope_temp;
    }
}


void task_datalogger(void *pvParameters) {
    FATFS fs;
    FRESULT f_res;
    FIL fp;
    UINT bw;

    datalogger_t data_to_log[LOGGER_CHUNK_SIZE] = {0};
    uint8_t data_index = -1;
    sd_event_t sd_event = {0};
    char buffer[128] = {0};
    char filename[64] = {0};
    BaseType_t f_res_status;
    uint8_t last_enable_index = 0;
    
    time_t current_time;
    i2c_guard_t guard_data = {
        .device = I2C_RTC,
        .queue = NULL,
        .callback = rtc_get_time_rtos,
        .param = (void *) &current_time
    };
    
    while(1) {
        if (!system_config.sd_mounted) {
            f_res = f_mount(&fs, "", 1);
            if (f_res != FR_OK) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                // printf("Error mounting SD card: %d\n", f_res);
                continue;
            }
            system_config.sd_mounted = true;
            data_index = 0;
            // printf("SD card mounted successfully.\n");
        }

        xQueueReceive(queue_datalogger, &sd_event, portMAX_DELAY);
        if (sd_event.type == LOG_FILE) {
            if (data_index < LOGGER_CHUNK_SIZE) {
                if (last_enable_index != enable_index) {
                    for (uint8_t i = 0; i < data_index; i++) {
                        data_to_log[i] = (datalogger_t) {0};
                    }
                    data_index = 0;
                    last_enable_index = enable_index;
                }
                data_to_log[data_index++] = sd_event.data;
            } else {
                xQueueSend(queue_i2c_guard, &guard_data, portMAX_DELAY);
                snprintf(filename, sizeof(filename), "log-%04d_%02d_%02d-%03d_%02d__%.2f_%.2f_%.2f__%04d.csv",
                    current_time.year, current_time.month, current_time.date,
                    system_config.resistance_target, enable_index,
                    Kp, Ki, Kd,
                    system_config.pid_time_ms / 100
                );
                f_res = f_open(&fp, filename, FA_WRITE | FA_OPEN_APPEND);
                if (f_res != FR_OK) {
                    system_config.sd_mounted = false;
                    break;
                }
                if (f_size(&fp) == 0) {
                    // Si el archivo está vacío, escribir encabezados
                    snprintf(buffer, sizeof(buffer), "Time;Voltage;Current;Error;PWM Value;Integral;Derivative;R_Target\n");
                    f_res = f_write(&fp, buffer, strlen(buffer), &bw);
                    if (f_res != FR_OK) {
                        printf("Error writing headers: %d\n", f_res);
                        f_close(&fp);
                        system_config.sd_mounted = false;
                        break;
                    }
                }
                // El puntero esta en la posicion final del archivo
                for (uint8_t i = 0; i < data_index; i++) {
                    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d;%.2f;%.2f;%.2f;%05d;%.2f;%.2f;%04d\n",
                        current_time.hour,
                        current_time.minute,
                        current_time.second,
                        data_to_log[i].voltage_v,
                        data_to_log[i].current_ma,
                        data_to_log[i].error,
                        data_to_log[i].pwm_value,
                        data_to_log[i].integral,
                        data_to_log[i].derivative,
                        data_to_log[i].r_target
                    );
                    f_res = f_write(&fp, buffer, strlen(buffer), &bw);
                    if (f_res != FR_OK) {
                        printf("Error writing to file: %d\n", f_res);
                        system_config.sd_mounted = false;
                        f_close(&fp);
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
                break;
            }
            snprintf(buffer, sizeof(buffer), "Menu: %d;Index: %d;Fixed Index: %d;SD Mounted: %d;PID Enabled: %d;PID Time: %d;PID Stable: %d;Resistance Target: %d;Resistance Adj: %d\n",
                system_config.menu,
                system_config.index,
                system_config.fixed_index,
                system_config.sd_mounted,
                system_config.pid_enabled,
                system_config.pid_time_ms,
                system_config.resistance_target,
                system_config.resistance_adj
            );
            f_res = f_write(&fp, buffer, strlen(buffer), &bw);
            if (f_res != FR_OK) {
                printf("Error writing to file: %d\n", f_res);
            }
            f_close(&fp);
        }
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
    queue_input_data = xQueueCreate(3, sizeof(input_data_t));
    if (queue_input_data == NULL) {
        printf("Error al crear la cola de input_data\n");
        return -1;
    }
    queue_rtc_time = xQueueCreate(1, sizeof(time_t));
    if (queue_rtc_time == NULL) {
        printf("Error al crear la cola de tiempo RTC\n");
        return -1;
    }

    queue_datalogger = xQueueCreate(4, sizeof(sd_event_t));
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

    rtc_set_i2c(I2C_PORT);

    // Creacion de tareas
    xTaskCreate(
        task_menu, "task_menu", configMINIMAL_STACK_SIZE * 3,
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
        task_ina219, "task_ina219", configMINIMAL_STACK_SIZE * 1,
        NULL, 2, NULL
    );
    // xTaskCreate(
    //     task_datalogger, "task_datalogger", configMINIMAL_STACK_SIZE * 10,
    //     NULL, 1, NULL
    // );
    xTaskCreate(
        task_btn_pull_up, "task_btn_menu", configMINIMAL_STACK_SIZE * 1,
        &btn_data_1, 1, NULL
    );
    xTaskCreate(
        task_btn_pull_up, "task_btn_clk", configMINIMAL_STACK_SIZE * 1,
        &btn_data_3, 1, NULL
    );
    // xTaskCreate(
    //     task_rtc, "task_rtc", configMINIMAL_STACK_SIZE * 1,
    //     NULL, 1, NULL
    // );
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
    ina219.max_expected_amps = INA219_MAX_CURRENT / 1000.0f; // Convertir a amperios
    ina219.shunt_resistor_value = 0.1f;
    ina219.gain = INA219_GAIN_1_40MV;

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
