// Barometer test: scans the I2C bus, finds the BMP280, then prints one
// reading per second as CSV over serial. Altitude is relative to the first
// reading, so lifting the board by a meter should show about +1 m.
//
// Flash: pio run -e baro-test -t upload && pio device monitor
//
// Serial commands (type a letter, then Enter):
//   z   zero the altitude here

#include <Arduino.h>
#include <Wire.h>
#include "baro.h"
#include "ride.h"

// Shared I2C bus with the RTC (0x51) and SHTC3 (0x70)
// Source: vendor/.../Arduino/*/user_config.h, ESP32_I2C_SDA_PIN / _SCL_PIN
#define I2C_SDA_PIN 47
#define I2C_SCL_PIN 48

static float    zero_m  = NAN;
static float    min_m   = INFINITY, max_m = -INFINITY;
static uint32_t samples = 0;

static const char *device_name(uint8_t a) {
    switch (a) {
    case 0x51: return "PCF85063 RTC (onboard)";
    case 0x70: return "SHTC3 temp/humidity (onboard)";
    case 0x18: return "ES8311 audio codec (onboard, needs audio power)";
    case 0x76:
    case 0x77: return "BMP280/BME280 barometer";
    default:   return "unknown";
    }
}

static void scan() {
    Serial.println("# I2C scan:");
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("#   0x%02X  %s\n", a, device_name(a));
            found++;
        }
    }
    if (!found) Serial.println("#   nothing found: check wiring and power");
}

void setup() {
    Serial.begin(115200);
    delay(2000);  // give the USB serial monitor time to connect

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
    scan();

    uint8_t id = baro_begin();
    if (!id) {
        Serial.println("# no BMP280/BME280 at 0x76 or 0x77. Check: VCC to 3V3, "
                       "GND, SDA to GPIO 47, SCL to GPIO 48.");
        return;
    }
    Serial.printf("# found %s (chip ID 0x%02X) at 0x%02X\n",
                  id == BME280_CHIP_ID ? "BME280" : "BMP280", id, baro_address());
    Serial.println("# sample,temp_c,pressure_pa,altitude_m,relative_m,noise_m");
}

void loop() {
    if (!baro_address()) {
        delay(1000);
        return;
    }
    while (Serial.available()) {
        if (Serial.read() == 'z') {
            zero_m = NAN;
            Serial.println("# zeroed");
        }
    }

    uint32_t t0 = millis();
    float temp_c, pressure_pa;
    if (!baro_read(&temp_c, &pressure_pa)) {
        Serial.println("# read failed");
    } else {
        float alt = pressure_to_altitude_m(pressure_pa, SEA_LEVEL_PA);
        if (isnan(zero_m)) {
            zero_m  = alt;
            min_m   = INFINITY;
            max_m   = -INFINITY;
            samples = 0;
        }
        float rel = alt - zero_m;
        samples++;
        if (rel < min_m) min_m = rel;
        if (rel > max_m) max_m = rel;
        // noise_m: spread since the last zero. Leave the board still to see
        // the sensor noise.
        Serial.printf("%lu,%.2f,%.1f,%.1f,%.2f,%.2f\n", samples, temp_c, pressure_pa,
                      alt, rel, max_m - min_m);
    }

    uint32_t spent = millis() - t0;
    if (spent < 1000) delay(1000 - spent);
}
