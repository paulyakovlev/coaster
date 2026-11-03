// Coaster firmware: buttons, ride recording and the screen.
//
// No GPS yet: while recording, a fake ride (lib/fake_ride) supplies one fix
// per second. GPX lines are formatted but not stored until there's storage.
//
// Buttons: BOOT = next page. PWR = ride control (see lib/ui/ui.h).

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "button.h"
#include "display.h"
#include "fake_ride.h"
#include "ride.h"
#include "screen.h"
#include "ui.h"

// Both buttons pull the pin low when pressed.
// Source: vendor/.../Arduino/*/user_config.h and src/button_bsp/button_bsp.c
#define BOOT_BUTTON_PIN 0
#define PWR_BUTTON_PIN  18

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

static const char *const STATE_NAMES[] = {"idle", "waking", "riding", "paused", "finished"};

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

void setup() {
    Serial.begin(115200);
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(PWR_BUTTON_PIN, INPUT_PULLUP);

    ride_stats_init(&stats);
    ui_init(&ui, millis());
    ui_render(&ui, &stats, canvas);
    display_begin(canvas);  // full refresh with the idle screen

    inputs = xQueueCreate(8, sizeof(UiInput));
    xTaskCreate(button_task, "buttons", 2048, nullptr, 2, nullptr);
}

void loop() {
    uint32_t now = millis();

    UiInput in;
    while (xQueueReceive(inputs, &in, 0) == pdTRUE) {
        UiState before = ui.state;
        apply(ui_input(&ui, in, now), now);
        if (ui.state != before) Serial.printf("# %s\n", STATE_NAMES[ui.state]);
    }

    if (recording && (int32_t)(now - next_fix_ms) >= 0) {
        next_fix_ms += FIX_INTERVAL_MS;
        GpsFix fix;
        fake_ride_next(&fake, &fix);
        ride_stats_update(&stats, &fix);
        ui_ride_update(&ui, &stats, now);
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

    delay(10);
}
