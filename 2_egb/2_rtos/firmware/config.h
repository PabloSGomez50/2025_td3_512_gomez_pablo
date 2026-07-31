#ifndef __FIRMWARE_CONFIG_H__
#define __FIRMWARE_CONFIG_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "lcd.h"
#include "ina219.h"
#include "ds3231.h"
#include "sd_card.h"

// I2C definiciones
#define I2C_PORT i2c1
#define I2C_SDA 18
#define I2C_SCL 19
#define LCD_ADDR 0x27

#define MINIMUM_RESISTANCE 20
#define MAXIMUM_RESISTANCE 2000 // Resistencia máxima en Ohm

// Botones y Encoder
#define DEBOUNCE_TIME 50 // Tiempo de debounce en ms
#define BTN_MENU_GPIO 2
#define BTN_STOP_GPIO 5
#define BTN_SWITCH_GPIO 13
#define MAX_MENU_NUM 2

#define ENC_CHA_GPIO 15
#define ENC_CHB_GPIO 14
#define ENC_MAX_INDEX 2

// Controlador PID y protecciones
#define MAX_PWM_WRAP 4096
#define MAX_PWM_DUTY 4095
#define MIN_PWM_DUTY 20

// #define PWM_GAIN 1.372f
// #define MAX_PWM_VOUT (float) (3.7f / PWM_GAIN)
// #define MIN_PWM_VOUT (float) (3.02f / PWM_GAIN)
// #define MAX_PWM_DUTY (uint16_t) (MAX_PWM_WRAP * MAX_PWM_VOUT / 3.3f)
// #define MIN_PWM_DUTY (uint16_t) (MAX_PWM_WRAP * MIN_PWM_VOUT / 3.3f)


#define PID_ENABLE_PIN 16
#define PWM_PIN 17
#define ADC_DIODE_TEMP 0 // Pin 26
#define MIN_TEMP 50.0f
#define MAX_TEMP 130.0f
#define INA219_MAX_CURRENT 1.5f
#define INA219_GAIN_SELECTED INA219_GAIN_4_160MV
#define MIN_CURRENT 0.10f
#define MAX_CURRENT 0.850f
#define MIN_VOLTAGE 3.0f
#define MAX_VOLTAGE 12.0f


#define MAX_KP 20.0f
#define MAX_KI 0.5f
#define MAX_KD 0.2f
#define MAX_INTEGRAL_VALUE 40.0f
#define MAX_DERIVATIVE_VALUE 40.0f

#define MAX_TIME_TARGET 60000 // 60 segundos

#define CONTROLLER_REFRESH_MS 40
#define SLEEP_INA219    30
#define SLEEP_TIME_LCD  350 // Tiempo de espera en ms para la LCD

#define USE_SERIAL_LOGGER 1
#define LOGGER_CHUNK_SIZE 100
#define LOGGER_MIN_SEND 10

// EGB - UART
#define UART_ID uart0
#define UART_BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define RX_BUFFER_SIZE 32

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} rtc_data_t;

typedef enum {
    BTN_MENU,
    BTN_STOP,
    BTN_SWITCH,
    ENCODER
} btn_devices_enum;

typedef struct  {
    uint8_t gpio;
    SemaphoreHandle_t *sem_bin;
    btn_devices_enum device;
} btn_data_t;

typedef struct encoder_t {
    bool cha;
    bool chb;
} encoder_t;

typedef enum {
    MENU_MAIN = 255,
    MENU_SET_RESISTANCE = 0,
    MENU_PID = 1,
    MENU_TEST = 2,
    MENU_TIME = 3,
    MENU_SD = 4,
    // MENU_REG_FUENTE = 5,
    MENU_PROTECCION = 6
} menu_t;

typedef enum {
    OVER_VOLTAGE,
    OVER_CURRENT,
    OVER_TEMPERATURE
} protection_t;

typedef enum  {
    I2C_INA219,
    I2C_LCD,
    I2C_RTC
} i2c_devices_enum;

typedef struct {
    i2c_devices_enum device;
    QueueHandle_t queue;
    void (*callback)(void * param);
    void * param;
} i2c_guard_t;

typedef struct {
    btn_devices_enum device;
    bool increment;
} input_data_t;

typedef struct {
    menu_t menu;
    uint8_t index;
    bool fixed_index;
    
    float max_temp;
    float max_current;
    float max_voltage;

    uint16_t pwm_value;
    int16_t resistance_adj;
    uint8_t r_index;
} system_config_t;

typedef struct {
    float kp;
    float ki;
    float kd;

    float ki_limit;
    float kd_limit;
    uint16_t pid_time_ms;
    uint16_t resistance_target;
} pid_config_t;

typedef struct {
    float voltage_v;
    float current_ma;
    uint16_t pwm_value;
    float error;
    float integral;
    float derivative;
    uint16_t r_target;
    float temperature;
} datalogger_t;

typedef enum {
    CONFIG_FILE,
    LOG_FILE
} sd_input_t;

typedef struct {
    sd_input_t type;
    void *data;
    uint8_t chunk_index;
} sd_event_t;

const char file_header[] = "Voltage;Current;PWM Value;Error;Integral;Derivative;R_Target;Temperature\n";
const uint8_t R_STEPS[] = {1, 10, 50, 100, 250};

typedef enum {
    CMD_GET,
    CMD_SET,
    CMD_ECHO,
    CMD_START,
    CMD_STOP,
    CMD_UNKNOWN
} cmd_tipo_t;

typedef enum {
    VAR_KP,
    VAR_KD,
    VAR_KI,
    VAR_R_TARGET,
    VAR_TIME_TARGET,
    VAR_KI_LIM,
    VAR_KD_LIM,
    // VAR_PID_TIME,
    // VAR_INA_TIME,
    VAR_MAX_TEMP,
    VAR_MAX_CURRENT,
    VAR_MAX_VOLTAGE,
    VAR_DATE,
    VAR_TIME,
    GET_VOLT,
    GET_CURRENT,
    GET_TEMP,
    GET_STATUS,
    GET_PROTECTION,
    GET_SD,
    GET_UNKNOWN
} cmd_variable_t;

typedef struct {
    const char *cmd;
    cmd_variable_t var;
} var_map_t;


const var_map_t var_map[] = {
    {"status", GET_STATUS},
    {"protec", GET_PROTECTION},
    {"voltage", GET_VOLT},
    {"current", GET_CURRENT},
    {"temp", GET_TEMP},
    {"sd_card", GET_SD},
    {"kp", VAR_KP},
    {"kd", VAR_KD},
    {"ki", VAR_KI},
    {"r_target", VAR_R_TARGET},
    {"t_target", VAR_TIME_TARGET},
    {"int_lim", VAR_KI_LIM},
    {"der_lim", VAR_KD_LIM},
    // {"pid_time", VAR_PID_TIME},
    // {"ina_time", VAR_INA_TIME},
    {"max_temp", VAR_MAX_TEMP},
    {"max_current", VAR_MAX_CURRENT},
    {"max_voltage", VAR_MAX_VOLTAGE},
    {""}
};



void btn_irq_handler(uint gpio, uint32_t events);
void task_encoder(void *pvParameters);
void task_btn_pull_up(void *pvParameters);
void task_pid_controller(void *pvParameters);
void task_i2c_guard(void *pvParameters);
void task_ina219(void *pvParameters);
void task_lcd_display(void *pvParameters);
void task_read_temp(void *pvParameters);
void task_rtc(void *pvParameters);
void task_uart(void *pvParameters);

void task_btn_stop_pull_up(void *pvParameters);

void uart_irq_handler(void);

uint8_t start_pid();
uint8_t stop_pid();
void setup_uart();
void setup_pwm(uint8_t gpio);
void set_lcd_text(void *text);
void limit_float(float *value, float max);
void wrap_index(uint8_t *index, uint8_t max_index, bool increment);

// void proces_command(char *cmd);


#endif // __FIRMWARE_CONFIG_H__