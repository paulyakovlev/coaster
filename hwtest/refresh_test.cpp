// Refresh test: one partial refresh per second, like the 1 Hz GPS fix, with
// the speed screen and the rider changing pose. A full refresh every N
// partials. Prints one CSV line per refresh over serial.
//
// Flash: pio run -e refresh-test -t upload && pio device monitor
//
// Serial commands (type a letter, then Enter):
//   f   full refresh now
//   +   full refresh every 5 more partials
//   -   full refresh every 5 fewer partials (0 = never)
//   r   rider on/off
//   p   pause/resume

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "display.h"
#include "screen.h"

// Battery voltage: ADC1 channel 3 (GPIO 4 on the ESP32-S3) behind a 1:2
// divider. Source: vendor/.../Arduino/01_ADC_Test/adc_bsp.cpp
#define VBAT_ADC_PIN 4

#define TICK_MS 1000

// Waveshare's FAQ suggests a full refresh after 5 partials; the real number
// is what this test is for.
static int  full_every = 5;
static bool show_rider = true;
static bool paused     = false;

static GFXcanvas1 canvas(SCREEN_WIDTH, SCREEN_HEIGHT);

static uint32_t tick       = 0;  // seconds since start
static uint32_t partials   = 0;  // partials since the last full refresh
static uint32_t next_tick  = 0;  // millis() of the next refresh
static bool     force_full = false;

// Fake ride: speed sweeps 0 -> 45 -> 0 km/h over 90 s, with a 10 s stop
static float fake_speed(uint32_t t) {
    uint32_t s = t % 100;
    if (s < 10) return 0;
    s -= 10;
    return s < 45 ? s : 90 - s;
}

// Test mapping only; the real rule waits on the UI and on cruising art
static RiderPose fake_pose(float speed) {
    if (speed < 1) return RIDER_STOPPED;
    if (speed < 15) return RIDER_CLIMBING;
    return RIDER_FAST;
}

static void handle_serial() {
    while (Serial.available()) {
        switch (Serial.read()) {
        case 'f': force_full = true; break;
        case '+': full_every += 5; break;
        case '-': full_every = full_every >= 5 ? full_every - 5 : 0; break;
        case 'r': show_rider = !show_rider; break;
        case 'p': paused = !paused; break;
        default: continue;  // Enter, stray bytes
        }
        Serial.printf("# full_every=%d rider=%d paused=%d\n", full_every, show_rider, paused);
    }
}

void setup() {
    Serial.begin(115200);
    analogReadResolution(12);

    screen_draw_speed(canvas, 0, show_rider, RIDER_STOPPED);
    uint32_t t0 = millis();
    display_begin(canvas);
    Serial.printf("# begin (init + full refresh) %lu ms\n", millis() - t0);
    Serial.println("# tick,type,speed,pose,refresh_ms,overrun,vbat_mv,full_every");

    next_tick = millis() + TICK_MS;
}

void loop() {
    handle_serial();
    if ((int32_t)(millis() - next_tick) < 0) {
        delay(10);
        return;
    }
    next_tick += TICK_MS;
    if (paused) return;

    tick++;
    float     speed = fake_speed(tick);
    RiderPose pose  = fake_pose(speed);
    screen_draw_speed(canvas, speed, show_rider, pose);

    bool full = force_full || (full_every > 0 && partials >= (uint32_t)full_every);
    uint32_t t0 = millis();
    if (full) {
        display_full(canvas);
        partials   = 0;
        force_full = false;
    } else {
        display_partial(canvas);
        partials++;
    }
    uint32_t ms = millis() - t0;

    // Refresh took longer than a tick: skip ahead instead of catching up
    bool overrun = (int32_t)(millis() - next_tick) >= 0;
    if (overrun) next_tick = millis() + TICK_MS;

    uint32_t vbat_mv = analogReadMilliVolts(VBAT_ADC_PIN) * 2;
    Serial.printf("%lu,%s,%d,%d,%lu,%d,%lu,%d\n", tick, full ? "full" : "partial",
                  (int)lroundf(speed), pose, ms, overrun, vbat_mv, full_every);
}
