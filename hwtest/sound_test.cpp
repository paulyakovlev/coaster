// Sound test: plays the ride start sound through the onboard ES8311 codec
// and speaker amplifier. Needs a speaker in the 2-pin speaker socket.
//
// Flash: pio run -e sound-test -t upload && pio device monitor
//
// Buttons: BOOT or PWR = start sound.
//
// Serial commands (type a letter or digit, then Enter):
//   1    start sound
//   t    1 kHz test tone, 1 s
//   + -  volume up / down 6 dB
//   p    toggle power-per-sound: codec off between sounds, as the main
//        firmware would do to save battery. Prints the wake-up time.

#include <Arduino.h>
#include <Wire.h>
#include "button.h"
#include "sound.h"

// Source: vendor/.../Arduino/*/user_config.h
#define I2C_SDA_PIN     47
#define I2C_SCL_PIN     48
#define BOOT_BUTTON_PIN 0
#define PWR_BUTTON_PIN  18

static Button  boot, pwr;
static bool    power_per_sound = false;

static bool wake() {
    uint32_t t0 = millis();
    if (!sound_begin()) {
        Serial.println("# codec did not respond at 0x18");
        return false;
    }
    Serial.printf("# codec on in %lu ms\n", (unsigned long)(millis() - t0));
    return true;
}

static void play() {
    if (power_per_sound && !wake()) return;
    uint32_t t0 = millis();
    sound_play_start();
    Serial.printf("# start (%d ms, played in %lu ms)\n", SOUND_START_MS,
                  (unsigned long)(millis() - t0));
    if (power_per_sound) sound_end();
}

static void print_volume() {
    Serial.printf("# volume 0x%02X (%+.1f dB)\n", sound_volume(), (sound_volume() - 0xBF) * 0.5f);
}

void setup() {
    Serial.begin(115200);
    delay(2000);  // give the USB serial monitor time to connect

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(PWR_BUTTON_PIN, INPUT_PULLUP);
    uint32_t now = millis();
    button_init(&boot, digitalRead(BOOT_BUTTON_PIN) == LOW, now);
    button_init(&pwr, digitalRead(PWR_BUTTON_PIN) == LOW, now);

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
    if (!wake()) return;
    print_volume();
    Serial.println("# 1 start sound, t tone, +/- volume, p power-per-sound. "
                   "BOOT or PWR = start sound.");
    play();
}

void loop() {
    uint32_t now = millis();

    bool boot_pressed = button_update(&boot, digitalRead(BOOT_BUTTON_PIN) == LOW, now) == BUTTON_PRESS;
    bool pwr_pressed  = button_update(&pwr, digitalRead(PWR_BUTTON_PIN) == LOW, now) == BUTTON_PRESS;
    if (boot_pressed || pwr_pressed) play();

    while (Serial.available()) {
        char c = Serial.read();
        if (c == '1') {
            play();
        } else if (c == 't') {
            if (power_per_sound && !wake()) continue;
            sound_tone(1000, 1000);
            if (power_per_sound) sound_end();
        } else if (c == '+' || c == '-') {
            int v = sound_volume() + (c == '+' ? 12 : -12);
            sound_set_volume((uint8_t)constrain(v, 0, 0xFF));
            print_volume();
        } else if (c == 'p') {
            power_per_sound = !power_per_sound;
            if (power_per_sound) sound_end();
            else wake();
            Serial.printf("# power per sound %s\n", power_per_sound ? "on" : "off");
        }
    }
    delay(5);
}
