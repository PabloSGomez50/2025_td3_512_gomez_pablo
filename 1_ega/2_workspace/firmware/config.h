#ifndef __FIRMWARE_CONFIG_H__
#define __FIRMWARE_CONFIG_H__

#include <stdio.h>
#include <string.h>
#include "hardware/i2c.h"

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

#define SLEEP_TIME_LCD  250 // Tiempo de espera en ms para la LCD

#define INA219_MAX_CURRENT 0.75f
#define SLEEP_I2C_GUARD 20 // Tiempo de espera en ms para el guardia I2C
#define SLEEP_INA219    50

#define MINIMUM_RESISTANCE 10
#define RESISTANCE_STEP 10 // Paso de resistencia en Ohm

// Botones y Encoder
#define DEBOUNCE_TIME 50 // Tiempo de debounce en ms
#define BTN_MENU_GPIO 6
#define BTN_STOP_GPIO 7
#define BTN_SWITCH_GPIO 14
#define MAX_MENU_NUM 2

#define ENC_CHA_GPIO 12
#define ENC_CHB_GPIO 13
#define ENC_MAX_INDEX 2

// Controlador PID y protecciones
#define PWM_GAIN 1.4f
#define MAX_PWM_WRAP 16000

#define MAX_PWM_VOUT (float) (4.55f / PWM_GAIN)
#define MIN_PWM_VOUT (float) (3.02f / PWM_GAIN)

#define MAX_PWM_DUTY (uint16_t) (MAX_PWM_WRAP * MAX_PWM_VOUT / 3.3f)
#define MIN_PWM_DUTY (uint16_t) (MAX_PWM_WRAP * MIN_PWM_VOUT / 3.3f)

#define PID_STATUS_PIN 15
#define PWM_PIN 16
#define ADC_DIODE_TEMP 0 // Pin 26
#define TEMP_THRESHOLD 130.0f
#define Kp 0.3f
#define Kd 0.0f
#define Ki 0.0f
#define MAX_INTEGRAL_VALUE 1000
#define CONTROLLER_REFRESH_MS 100


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
    MENU_PID_TUNING = 1,
    MENU_TEST = 2,
    MENU_TIME = 3,
    MENU_SD = 4,
    MENU_REG_FUENTE = 5
} menu_t;

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

    bool sd_mounted;

    bool pid_enabled;
    bool pid_escalon;
    bool pid_stable;
    uint16_t resistance_target;
    uint16_t resistance_adj;
} system_config_t;

void btn_irq_handler(uint gpio, uint32_t events);
void task_encoder(void *pvParameters);
void task_btn_pull_up(void *pvParameters);
void task_pid_controller(void *pvParameters);
void task_i2c_guard(void *pvParameters);
void task_ina219(void *pvParameters);
void task_rtc(void *pvParameters);
void task_lcd_display(void *pvParameters);
void task_read_temp(void *pvParameters);

void setup_pwm(uint8_t gpio);
void set_lcd_text(void *text);

#endif // __FIRMWARE_CONFIG_H__