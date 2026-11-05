// Coaster firmware: buttons, ride recording and the screen.
//
// No GPS yet: while recording, a fake ride (lib/fake_ride) supplies one fix
// per second. GPX lines are formatted but not stored until there's storage.
//
// Buttons: BOOT = next page. PWR = ride control (see lib/ui/ui.h).
// Starting a ride plays one short sound; nothing else beeps (group rides).
// Idle for a while: the sleep screen, then deep sleep until a button press.

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Wire.h>
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "battery.h"
#include "button.h"
#include "display.h"
#include "fake_ride.h"
#include "ride.h"
#include "screen.h"
#include "sound.h"
#include "ui.h"

// Both buttons pull the pin low when pressed.
// Source: vendor/.../Arduino/*/user_config.h and src/button_bsp/button_bsp.c
#define BOOT_BUTTON_PIN 0
#define PWR_BUTTON_PIN  18

// Shared I2C bus: audio codec now, barometer later.
// Source: vendor/.../Arduino/*/user_config.h
#define I2C_SDA_PIN     47
#define I2C_SCL_PIN     48

// Battery voltage: ADC1 channel 3 (GPIO 4 on the ESP32-S3) behind a 1:2
// divider. Source: vendor/.../Arduino/01_ADC_Test/adc_bsp.cpp
#define VBAT_ADC_PIN    4
// Battery power hold: high keeps the board on when running from the battery.
// Source: vendor/.../Arduino/*/user_config.h (VBAT_PWR_PIN)
#define VBAT_HOLD_PIN   17
#define BATTERY_EVERY_MS 60000

// Codec DAC volume: 0xBF = 0 dB, 0.5 dB per step, 0xFF = +32 dB
#define SOUND_VOLUME    0xBF

#define BUTTON_POLL_MS  10
#define FIX_INTERVAL_MS 1000

// Start time for the fake ride's GPX timestamps, until the GPS gives real time
#define FAKE_START_TIME 1793690142u  // 2026-11-03T07:15:42Z

static GFXcanvas1    canvas(SCREEN_WIDTH, SCREEN_HEIGHT);
static Ui            ui;
static RideStats     stats;
static FakeRide      fake;
static bool          recording = false;
static uint32_t      next_fix_ms;
static QueueHandle_t inputs;
static TaskHandle_t  sound_task_handle;
static uint32_t      battery_read_ms;

static const char *const STATE_NAMES[] = {"idle", "waking", "riding", "paused", "finished",
                                          "sleeping"};

// Screen refreshes block for 0.6-2 s, so buttons are read in their own task
// and queued; no press is lost during a refresh.
static void button_task(void *) {
    Button boot, pwr;
    uint32_t now = millis();
    button_init(&boot, digitalRead(BOOT_BUTTON_PIN) == LOW, now);
    // Held at startup = the press that powered the board on: ignored
    button_init(&pwr, digitalRead(PWR_BUTTON_PIN) == LOW, now);

    for (;;) {
        now = millis();
        UiInput in;
        if (button_update(&boot, digitalRead(BOOT_BUTTON_PIN) == LOW, now) == BUTTON_PRESS) {
            in = UI_IN_PAGE;
            xQueueSend(inputs, &in, 0);
        }
        switch (button_update(&pwr, digitalRead(PWR_BUTTON_PIN) == LOW, now)) {
        case BUTTON_PRESS: in = UI_IN_PWR; xQueueSend(inputs, &in, 0); break;
        case BUTTON_LONG:  in = UI_IN_PWR_LONG; xQueueSend(inputs, &in, 0); break;
        case BUTTON_NONE:  break;
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

// The start sound plays in its own task, so it doesn't hold up the screen
// refresh to the first ride page. The codec and amplifier are powered only while it plays.
static void sound_task(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (sound_begin()) {
            sound_play_start();
        } else {
            Serial.println("# sound: codec did not respond");
        }
        sound_end();
    }
}

static void apply(UiAction action, uint32_t now) {
    switch (action) {
    case UI_ACT_START:
        ride_stats_init(&stats);
        fake_ride_init(&fake, FAKE_START_TIME);
        recording   = true;
        next_fix_ms = now;
        break;
    case UI_ACT_PAUSE:
        ride_stats_pause(&stats);
        recording = false;
        break;
    case UI_ACT_RESUME:
        recording   = true;
        next_fix_ms = now;
        break;
    case UI_ACT_FINISH:
        recording = false;
        Serial.printf("# ride: %.2f km, %lu s elapsed, %lu s moving, avg %.1f km/h, "
                      "max %.1f km/h, %.0f m up\n",
                      stats.distance_m / 1000, (unsigned long)ride_stats_elapsed_s(&stats),
                      (unsigned long)stats.moving_s, ride_stats_avg_speed_mps(&stats) * 3.6f,
                      stats.max_speed_mps * 3.6f, stats.ele_gain_m);
        break;
    case UI_ACT_NONE:
        break;
    }
}

// Battery voltage, averaged over a few reads against ADC noise
static void update_battery() {
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(VBAT_ADC_PIN);
    uint16_t mv = sum / 8 * 2;  // undo the 1:2 divider
    ui.battery_pct = battery_percent(mv);
    Serial.printf("# battery %u mV %d%%\n", mv, ui.battery_pct);
}

// Deep sleep, leaving the sleep screen on the panel. Same sequence as the
// vendor's 11_RTC_Sleep_Test (EnableDeepLowPowerMode): hold the battery power
// pin high through sleep, wake when BOOT or PWR pulls low. Waking restarts
// the firmware from setup().
static void sleep_now() {
    Serial.println("# deep sleep");
    Serial.flush();
    esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_AUTO);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_ext1_wakeup_io((1ULL << BOOT_BUTTON_PIN) | (1ULL << PWR_BUTTON_PIN),
                                    ESP_EXT1_WAKEUP_ANY_LOW);
    rtc_gpio_hold_en((gpio_num_t)VBAT_HOLD_PIN);
    esp_deep_sleep_start();
}

void setup() {
    Serial.begin(115200);
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(PWR_BUTTON_PIN, INPUT_PULLUP);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);

    ride_stats_init(&stats);
    ui_init(&ui, millis());
    update_battery();
    battery_read_ms = millis();
    ui_render(&ui, &stats, canvas);
    display_begin(canvas);  // full refresh with the idle screen
    gpio_hold_dis((gpio_num_t)VBAT_HOLD_PIN);  // held through sleep; display_begin set it high
    sound_end();            // display power setup leaves the audio rail on
    sound_set_volume(SOUND_VOLUME);

    inputs = xQueueCreate(8, sizeof(UiInput));
    xTaskCreate(button_task, "buttons", 2048, nullptr, 2, nullptr);
    xTaskCreate(sound_task, "sound", 4096, nullptr, 1, &sound_task_handle);
}

void loop() {
    uint32_t now = millis();

    UiInput in;
    while (xQueueReceive(inputs, &in, 0) == pdTRUE) {
        UiState  before = ui.state;
        UiAction action = ui_input(&ui, in, now);
        apply(action, now);
        if (action == UI_ACT_START) xTaskNotifyGive(sound_task_handle);
        if (ui.state != before) Serial.printf("# %s\n", STATE_NAMES[ui.state]);
    }

    if (recording && (int32_t)(now - next_fix_ms) >= 0) {
        next_fix_ms += FIX_INTERVAL_MS;
        GpsFix fix;
        fake_ride_next(&fake, &fix);
        ride_stats_update(&stats, &fix);
        ui_ride_update(&ui, &stats, now);
    }

    if (now - battery_read_ms >= BATTERY_EVERY_MS) {
        battery_read_ms = now;
        update_battery();
    }

    UiState before = ui.state;
    ui_tick(&ui, now);
    if (ui.state != before) Serial.printf("# %s\n", STATE_NAMES[ui.state]);

    UiRefresh r = ui_render(&ui, &stats, canvas);
    if (r != UI_REFRESH_NONE) {
        uint32_t t0 = millis();
        if (r == UI_REFRESH_FULL) {
            display_full(canvas);
        } else {
            display_partial(canvas);
        }
        Serial.printf("# %s refresh %lu ms\n", r == UI_REFRESH_FULL ? "full" : "partial",
                      (unsigned long)(millis() - t0));
    }
    if (ui.shown.state == UI_SLEEPING) sleep_now();  // sleep screen is on the panel

    delay(10);
}
