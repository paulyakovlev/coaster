#include "baro.h"

// Compensation formulas from the Bosch BMP280 datasheet (BST-BMP280-DS001),
// section 8.2, 32-bit temperature and 64-bit pressure versions.

static uint16_t u16(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }
static int16_t  s16(const uint8_t *b) { return (int16_t)u16(b); }

void bmp280_parse_calib(const uint8_t raw[24], Bmp280Calib *c) {
    c->T1 = u16(raw + 0);
    c->T2 = s16(raw + 2);
    c->T3 = s16(raw + 4);
    c->P1 = u16(raw + 6);
    c->P2 = s16(raw + 8);
    c->P3 = s16(raw + 10);
    c->P4 = s16(raw + 12);
    c->P5 = s16(raw + 14);
    c->P6 = s16(raw + 16);
    c->P7 = s16(raw + 18);
    c->P8 = s16(raw + 20);
    c->P9 = s16(raw + 22);
}

void bmp280_compensate(const Bmp280Calib *c, int32_t adc_t, int32_t adc_p,
                       float *temp_c, float *pressure_pa) {
    int32_t v1 = ((((adc_t >> 3) - ((int32_t)c->T1 << 1))) * c->T2) >> 11;
    int32_t dt = (adc_t >> 4) - (int32_t)c->T1;
    int32_t v2 = (((dt * dt) >> 12) * c->T3) >> 14;
    int32_t t_fine = v1 + v2;
    *temp_c = ((t_fine * 5 + 128) >> 8) / 100.0f;

    int64_t var1 = (int64_t)t_fine - 128000;
    int64_t var2 = var1 * var1 * c->P6;
    var2 += (var1 * c->P5) << 17;
    var2 += (int64_t)c->P4 << 35;
    var1 = ((var1 * var1 * c->P3) >> 8) + ((var1 * c->P2) << 12);
    var1 = ((((int64_t)1 << 47) + var1) * c->P1) >> 33;
    if (var1 == 0) {
        *pressure_pa = 0;  // avoid division by zero
        return;
    }
    int64_t p = 1048576 - adc_p;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)c->P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)c->P8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((int64_t)c->P7 << 4);
    *pressure_pa = p / 256.0f;  // Q24.8
}
