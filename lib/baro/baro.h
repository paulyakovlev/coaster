#pragma once

#include <stdint.h>

// BMP280 barometer (BME280 also works; its humidity part is unused).
//
// Two halves:
// - bmp280_compensate(): raw readings -> temperature and pressure. Pure math
//   from the Bosch datasheet, no hardware, unit-tested in test/.
// - baro_begin() / baro_read(): I2C, ESP32 only. Uses Wire, which the caller
//   starts with the board's pins.

#define BMP280_CHIP_ID 0x58
#define BME280_CHIP_ID 0x60

// Factory calibration, read once from the sensor (registers 0x88-0x9F)
struct Bmp280Calib {
    uint16_t T1;
    int16_t  T2, T3;
    uint16_t P1;
    int16_t  P2, P3, P4, P5, P6, P7, P8, P9;
};

// Unpack the 24 calibration bytes, little endian as on the chip
void bmp280_parse_calib(const uint8_t raw[24], Bmp280Calib *c);

// Raw 20-bit readings -> deg C and Pa. Datasheet section 8.2, integer version.
void bmp280_compensate(const Bmp280Calib *c, int32_t adc_t, int32_t adc_p,
                       float *temp_c, float *pressure_pa);

// ---- I2C (ESP32 only) ---------------------------------------------------------

// Look for the sensor at 0x76, then 0x77. Returns the chip ID (0x58 BMP280,
// 0x60 BME280), or 0 if nothing answered. Sets the sensor to sleep between
// readings: each baro_read() takes one measurement (forced mode).
uint8_t baro_begin();

// I2C address found by baro_begin()
uint8_t baro_address();

// One measurement, about 45 ms. Returns false on an I2C error or timeout.
bool baro_read(float *temp_c, float *pressure_pa);
