// I2C half of the barometer driver. ESP32 only; the math half builds anywhere.
#ifdef ARDUINO_ARCH_ESP32

#include "baro.h"

#include <Arduino.h>
#include <Wire.h>

#define REG_CALIB     0x88
#define REG_CHIP_ID   0xD0
#define REG_RESET     0xE0
#define REG_STATUS    0xF3
#define REG_CTRL_MEAS 0xF4
#define REG_CONFIG    0xF5
#define REG_DATA      0xF7

#define STATUS_MEASURING 0x08
#define STATUS_IM_UPDATE 0x01

// ctrl_meas: temperature x2, pressure x16 oversampling ("ultra high
// resolution", datasheet table 7), forced mode. About 43 ms per reading.
#define CTRL_MEAS_FORCED ((2 << 5) | (5 << 2) | 1)

// config: IIR filter coefficient 4 against gusts and door slams. Standby
// time is unused in forced mode.
#define CONFIG_VALUE (2 << 2)

static uint8_t     addr;
static Bmp280Calib calib;

static bool write_reg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool read_regs(uint8_t reg, uint8_t *buf, uint8_t len) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

uint8_t baro_begin() {
    const uint8_t candidates[] = {0x76, 0x77};
    for (uint8_t a : candidates) {
        addr = a;
        uint8_t id;
        if (!read_regs(REG_CHIP_ID, &id, 1)) continue;
        if (id != BMP280_CHIP_ID && id != BME280_CHIP_ID) continue;

        write_reg(REG_RESET, 0xB6);
        delay(5);
        // Wait for the calibration data to be copied from NVM
        uint8_t status = STATUS_IM_UPDATE;
        for (int i = 0; i < 20 && (status & STATUS_IM_UPDATE); i++) {
            delay(2);
            if (!read_regs(REG_STATUS, &status, 1)) return 0;
        }

        uint8_t raw[24];
        if (!read_regs(REG_CALIB, raw, sizeof(raw))) return 0;
        bmp280_parse_calib(raw, &calib);
        if (!write_reg(REG_CONFIG, CONFIG_VALUE)) return 0;
        return id;
    }
    addr = 0;
    return 0;
}

uint8_t baro_address() {
    return addr;
}

bool baro_read(float *temp_c, float *pressure_pa) {
    if (!addr || !write_reg(REG_CTRL_MEAS, CTRL_MEAS_FORCED)) return false;

    // Datasheet max is 43.2 ms at these settings; poll until done
    delay(40);
    uint8_t status = STATUS_MEASURING;
    for (int i = 0; i < 20 && (status & STATUS_MEASURING); i++) {
        if (!read_regs(REG_STATUS, &status, 1)) return false;
        if (status & STATUS_MEASURING) delay(2);
    }
    if (status & STATUS_MEASURING) return false;

    uint8_t d[6];
    if (!read_regs(REG_DATA, d, sizeof(d))) return false;
    int32_t adc_p = (int32_t)d[0] << 12 | d[1] << 4 | d[2] >> 4;
    int32_t adc_t = (int32_t)d[3] << 12 | d[4] << 4 | d[5] >> 4;
    bmp280_compensate(&calib, adc_t, adc_p, temp_c, pressure_pa);
    return true;
}

#endif
