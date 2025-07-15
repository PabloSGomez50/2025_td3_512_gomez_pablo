#include "ina219.h"

static ina219_status_t read_register(ina219_t ina219, uint8_t reg, uint16_t *result) {
    uint8_t buf[2];

    i2c_write_timeout_us(ina219.i2c, ina219.addr, &reg, 1, true, TIMEOUT_US);
    if(i2c_read_timeout_us(ina219.i2c, ina219.addr, buf, 2, false, TIMEOUT_US) == PICO_ERROR_TIMEOUT) {
        return INA219_TIMEOUT;
    }

    *result = (buf[0] << 8) | buf[1];
    return INA219_OK;
}

static ina219_status_t write_register(ina219_t ina219, uint8_t reg, uint16_t value) {
    uint8_t buf[3];
    buf[0] = reg;
    buf[1] = (value >> 8) & 0xFF;
    buf[2] = value & 0xFF;
    return (ina219_status_t) i2c_write_timeout_us(ina219.i2c, ina219.addr, buf, 3, true, TIMEOUT_US);
}

ina219_status_t ina219_init(ina219_t ina219) {
    uint16_t config = 0x399F;
    ina219_status_t status = write_register(ina219, INA219_REG_CONFIG, config);
    
    return status;
}

ina219_status_t ina219_calibrate(ina219_t ina219) {
    ina219._current_lsb = ina219._max_expected_amps / 32768.0;
    uint16_t cal_reg_value = (uint16_t)(0.04096 / (ina219._current_lsb * ina219._shunt_resistor_value));
    return write_register(ina219, INA219_REG_CALIBRATION, cal_reg_value);
}

ina219_status_t ina219_read_voltage(ina219_t ina219, float *voltage) {   
    uint16_t value;
    ina219_status_t status = read_register(ina219, INA219_REG_BUSVOLTAGE, &value);
    *voltage = (value >> 3) * 0.004;
    return status;
}

ina219_status_t ina219_read_shunt_voltage(ina219_t ina219, float *shunt_voltage) {
    int16_t value;
    ina219_status_t status = read_register(ina219, INA219_REG_SHUNTVOLTAGE, &value);
    *shunt_voltage = value * 0.01;
    return status;
}

ina219_status_t ina219_read_current(ina219_t ina219, float *current) {
    int16_t value;
    ina219_status_t status = read_register(ina219, INA219_REG_CURRENT, &value);
    *current = value * ina219._current_lsb;
    return status;
}

ina219_status_t ina219_read_power(ina219_t ina219, float *power) {
    uint16_t value;
    ina219_status_t status = read_register(ina219, INA219_REG_POWER, &value);
    *power = value * 0.02;
    return status;
}

void ina219_get_data(void* ina219_ptr) {
    ina219_t ina219 = *(ina219_t *) ina219_ptr;
    ina219_data_t data;
    ina219_status_t status;

    status = ina219_read_voltage(ina219, &data.voltage_v);
    if (status != INA219_OK) {
        data.voltage_v = -1.0f;
    }

    status = ina219_read_current(ina219, &data.current_a);
    if (status != INA219_OK) {
        data.current_a = -1.0f;
    }

    status = ina219_read_power(ina219, &data.power_w);
    if (status != INA219_OK) {
        data.power_w = -1.0f;
    }
    printf("INA219 Data: Voltage: %.2f V, Current: %.2f A, Power: %.2f W\n", 
           data.voltage_v, data.current_a, data.power_w);
    // return data;
}

void ina219_init_and_calibrate(void * ina219_ptr) {
    ina219_t *ina219 = (ina219_t *) ina219_ptr;
    ina219_status_t status;

    status = ina219_init(*ina219);
    if (status != INA219_OK) {
        printf("Error initializing INA219\n");
        return;
    }

    status = ina219_calibrate(*ina219);
    if (status != INA219_OK) {
        printf("Error calibrating INA219\n");
        return;
    }

    printf("INA219 initialized and calibrated successfully\n");
}