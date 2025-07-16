#ifndef _INA219_H_
#define _INA219_H_

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "math.h"

#define INA219_I2C_ADDR         0x40
#define INA219_REG_CONFIG       0x00
#define INA219_REG_SHUNTVOLTAGE 0x01
#define INA219_REG_BUSVOLTAGE   0x02
#define INA219_REG_POWER        0x03
#define INA219_REG_CURRENT      0x04
#define INA219_REG_CALIBRATION  0x05

#define TIMEOUT_US              10000

typedef enum {
    INA219_OK,
    INA219_TIMEOUT = -2
} ina219_status_t;

typedef struct {
    i2c_inst_t *i2c;
    uint8_t addr;
    float _current_lsb;
    float _shunt_resistor_value;
    float _max_expected_amps;
} ina219_t;

typedef struct {
    float voltage_v;
    float current_a;
    float power_w;
} ina219_data_t;

typedef struct {
    ina219_data_t *data;
    ina219_t ina219;
} ina219_context_t;

// Prototipos

static inline ina219_t ina219_get_default_config(void) {
    return (ina219_t) {
        .i2c = i2c0,
        .addr = INA219_I2C_ADDR,
        ._max_expected_amps = 4.0f,
        ._shunt_resistor_value = 0.1f,
    };
}

ina219_status_t ina219_init(ina219_t ina219);
ina219_status_t ina219_calibrate(ina219_t ina219);
ina219_status_t ina219_read_voltage(ina219_t ina219, float *voltage);
ina219_status_t ina219_read_shunt_voltage(ina219_t ina219, float *shunt_voltage);
ina219_status_t ina219_read_current(ina219_t ina219, float *current);
ina219_status_t ina219_read_power(ina219_t ina219, float *power);

void ina219_init_and_calibrate(void * ina219_ptr);
void ina219_get_data(void * ina219);

#endif // INA219_H