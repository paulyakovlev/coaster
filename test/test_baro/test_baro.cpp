// Unit tests for the barometer math in lib/baro and lib/ride: pio test -e native

#include <unity.h>

#include "baro.h"
#include "ride.h"

void setUp() {}
void tearDown() {}

// Worked example from the Bosch BMP280 datasheet: these calibration values
// and raw readings give 25.08 deg C and 100653.27 Pa.
static void test_datasheet_example(void) {
    // Calibration as the chip stores it, little endian
    const int16_t words[12] = {27504, 26435, -1000, (int16_t)36477, -10685, 3024,
                               2855, 140, -7, 15500, -14600, 6000};
    uint8_t raw[24];
    for (int i = 0; i < 12; i++) {
        raw[2 * i]     = (uint8_t)(words[i] & 0xFF);
        raw[2 * i + 1] = (uint8_t)((uint16_t)words[i] >> 8);
    }
    Bmp280Calib c;
    bmp280_parse_calib(raw, &c);
    TEST_ASSERT_EQUAL_UINT16(27504, c.T1);
    TEST_ASSERT_EQUAL_UINT16(36477, c.P1);
    TEST_ASSERT_EQUAL_INT16(-14600, c.P8);

    float t, p;
    bmp280_compensate(&c, 519888, 415148, &t, &p);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 25.08f, t);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 100653.27f, p);
}

static void test_altitude(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0, pressure_to_altitude_m(SEA_LEVEL_PA, SEA_LEVEL_PA));
    // Standard atmosphere: 89874.6 Pa at 1000 m, 79495.2 Pa at 2000 m
    TEST_ASSERT_FLOAT_WITHIN(1, 1000, pressure_to_altitude_m(89874.6f, SEA_LEVEL_PA));
    TEST_ASSERT_FLOAT_WITHIN(2, 2000, pressure_to_altitude_m(79495.2f, SEA_LEVEL_PA));
    // Near sea level, 12 Pa is about 1 m
    float d = pressure_to_altitude_m(SEA_LEVEL_PA - 12, SEA_LEVEL_PA);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, d);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_datasheet_example);
    RUN_TEST(test_altitude);
    return UNITY_END();
}
