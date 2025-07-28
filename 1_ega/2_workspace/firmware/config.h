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

// I2C definiciones
#define I2C_PORT i2c1
#define I2C_SDA 18
#define I2C_SCL 19
#define LCD_ADDR 0x27

#define SLEEP_TIME_LCD  250 // Tiempo de espera en ms para la LCD

#define MAX_CURRENT_INA219 0.3f
#define SLEEP_INA219    100

#define SLEEP_I2C_GUARD 50 // Tiempo de espera en ms para el guardia I2C

// Botones y Encoder
#define DEBOUNCE_TIME 50 // Tiempo de debounce en ms
#define BTN_MENU_GPIO 14
#define BTN_STOP_GPIO 15
#define BTN_SWITCH_GPIO 13
#define MAX_MENU_NUM 2

#define ENC_CHA_GPIO 11
#define ENC_CHB_GPIO 12
#define ENC_MAX_INDEX 2

// Controlador PID y protecciones
#define MAX_PWM_DUTY 1024
#define PWM_PIN 16
#define ADC_DIODE_TEMP 0 // Pin 26
#define TEMP_THRESHOLD 130.0f
#define Kp 15
#define Kd 4
#define Ki 3.5
#define MAX_INTEGRAL_VALUE 1000.0f
#define CONTROLLER_REFRESH_MS 10

#define MINIMUM_RESISTANCE 2.0f

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
    // uint8_t *counter;
    // uint8_t max_counter;
} btn_data_t;

typedef struct encoder_t {
    bool cha;
    bool chb;
} encoder_t;

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
    uint8_t menu;
    uint8_t index;
    bool pid_enabled;
    float resistance_target;
    float resistance_adj;
    bool pid_escalon;
    bool pid_stable;
    ina219_data_t ina219_data;
} system_config_t;

void btn_irq_handler(uint gpio, uint32_t events);
void task_encoder(void *pvParameters);
void task_btn_pull_up(void *pvParameters);
void task_pid_controller(void *pvParameters);
void task_i2c_guard(void *pvParameters);
void task_ina219(void *pvParameters);
void task_lcd_display(void *pvParameters);
void task_read_temp(void *pvParameters);

void setup_pwm(uint8_t gpio);
void set_lcd_text(void *text);

#endif // __FIRMWARE_CONFIG_H__