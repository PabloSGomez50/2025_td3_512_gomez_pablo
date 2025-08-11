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
    uint8_t status = i2c_write_timeout_us(ina219.i2c, ina219.addr, buf, 3, true, TIMEOUT_US);

    return status == 3 ? INA219_OK : (ina219_status_t) status;
}

ina219_status_t ina219_init_and_calibrate(ina219_t ina219) {
    if (ina219.shunt_resistor_value <= 0.0f || ina219.max_expected_amps <= 0.0f) {
        return INA219_INVALID_PARAM; // Invalid configuration
    }
    float max_range = powf(2.0f, (float)ina219.gain) * 0.04f;
    if (max_range < (ina219.max_expected_amps * ina219.shunt_resistor_value)) {
        return INA219_INVALID_PARAM; // Invalid gain setting
    }
    uint16_t config = 0x399F & ~(0b11 << 11);
    config |= (ina219.gain << 11); // Set gain bits
    ina219_status_t status = write_register(ina219, INA219_REG_CONFIG, config);
    if (status != INA219_OK) {
        return INA219_INIT_ERROR; // Initialization error
    }
    
    ina219._current_lsb = ina219.max_expected_amps / 32768.0;
    uint16_t cal_reg_value = (uint16_t)(0.04096 / (ina219._current_lsb * ina219.shunt_resistor_value));
    status = write_register(ina219, INA219_REG_CALIBRATION, cal_reg_value);
    if (status != INA219_OK) {
        return INA219_CALIBRATION_ERROR; // Calibration error
    }
}

ina219_status_t ina219_read_data(ina219_t ina219, ina219_data_t *data) {
    if (data == NULL) {
        return INA219_INVALID_PARAM; // Invalid parameter
    }

    ina219_status_t status = ina219_read_voltage(ina219, &data->voltage_v);
    if (status != INA219_OK) return status;

    status = ina219_read_shunt_voltage(ina219, &data->shunt_voltage_v);
    if (status != INA219_OK) return status;

    status = ina219_read_current(ina219, &data->current_a);
    if (status != INA219_OK) return status;

    status = ina219_read_power(ina219, &data->power_w);
    return status;
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
    *power = value * 20.0f * ina219._current_lsb;
    return status;
}


void ina219_get_data_rtos(void* param_ptr) {
    ina219_context_t *context = (ina219_context_t *) param_ptr;

    ina219_t ina219 = context->ina219;
    ina219_data_t *data = context->data;
    ina219_status_t status;

    status = ina219_read_voltage(ina219, &data->voltage_v);
    if (status != INA219_OK) {
        data->voltage_v = -1.0f;
    }
    status = ina219_read_current(ina219, &data->current_a);
    if (status != INA219_OK) {
        data->current_a = -1.0f;
    }
    status = ina219_read_power(ina219, &data->power_w);
    if (status != INA219_OK) {
        data->power_w = -1.0f;
    }
}

void ina219_init_rtos(void * param_ptr) {
    ina219_context_t *context = (ina219_context_t *) param_ptr;

    ina219_t ina219 = context->ina219;
    ina219_status_t status;

    status = ina219_init_and_calibrate(ina219);
    if (status != INA219_OK) {
        printf("Error initializing and calibrating INA219\n");
        return;
    }
}