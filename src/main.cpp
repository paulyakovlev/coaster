// Coaster firmware: buttons, ride recording and the screen.
//
// Fixes come from the GPS over UART. The fake-ride build (env fake-ride)
// swaps in a made-up ride, one fix per second, for testing at a desk.
// Each ride is saved as a GPX file on the flash (lib/ride_log); one cut off
// by a power cut is repaired at the next boot.
//
// Buttons: BOOT = next page. PWR = ride control (see lib/ui/ui.h).
// BOOT long press from idle opens setup: home Wi-Fi, then Strava
// (src/wifi_setup.cpp). Back in idle, finished rides upload in the
// background (src/sync.cpp).
// Starting a ride plays one short sound; nothing else beeps (group rides).
// Idle for a while: the sleep screen, then deep sleep until a button press.

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <LittleFS.h>
#include <Wire.h>
#include "driver/rtc_io.h"
#include "esp_partition.h"
#include "esp_sleep.h"
#include "battery.h"
#include "button.h"
#include "display.h"
#include "fake_ride.h"
#include "ride.h"
#include "ride_log.h"
#include "screen.h"
#include "sound.h"
#include "sync.h"
#include "ui.h"
#include "wifi_setup.h"

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

// Rides live in the root of the LittleFS partition ("spiffs" in partitions.csv)
#define RIDES_DIR       "/littlefs"

static GFXcanvas1    canvas(SCREEN_WIDTH, SCREEN_HEIGHT);
static Ui            ui;
static RideStats     stats;
static bool          recording = false;
static RideLog       ride_log;
static QueueHandle_t inputs;
static TaskHandle_t  sound_task_handle;
static uint32_t      battery_read_ms;
static bool          setup_open = false;

#ifdef FAKE_RIDE

#define FIX_INTERVAL_MS 1000
// Start time for the fake ride's GPX timestamps
#define FAKE_START_TIME 1793690142u  // 2026-11-03T07:15:42Z

static FakeRide fake;
static uint32_t next_fix_ms;

static void fixes_begin() {}

// Ride starts or resumes: the next fix is due now
static void fixes_restart(bool new_ride, uint32_t now) {
    if (new_ride) fake_ride_init(&fake, FAKE_START_TIME);
    next_fix_ms = now;
}

// One fix per second while recording
static bool next_fix(uint32_t now, GpsFix *fix) {
    if (!recording || (int32_t)(now - next_fix_ms) < 0) return false;
    next_fix_ms += FIX_INTERVAL_MS;
    fake_ride_next(&fake, fix);
    return true;
}

#else

// GPS UART: module TX -> GP1 on header J1 (GPIO 1, per the schematic in
// vendor/.../04_Hardware/Schematics). Nothing goes back to the module: the
// firmware only listens, and GP2 stays free for a GPS power switch.
// -1 leaves TX unassigned; the core picks its default TX1 (GPIO 16, audio
// I2S) only when both pins are -1.
#define GPS_RX_PIN  1   // board RX <- module TX
#define GPS_TX_PIN  -1  // not connected

// ATGM336H (GPS + BeiDou): 9600 8N1 by default, per the ATGM336H-5N user
// manual (section 2.6), tinytronics.nl/product_files/002176_ATGM336H.pdf
#define GPS_BAUD    9600
// A full refresh blocks the loop for about 2.2 s. At 9600 baud the module can
// send at most 960 bytes a second: room for 4 s, so no sentence is lost.
#define GPS_RX_BUFFER 4096

static NmeaParser nmea;

static void fixes_begin() {
    Serial1.setRxBufferSize(GPS_RX_BUFFER);  // before begin(), or it's ignored
    Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    nmea_init(&nmea);
}

static void fixes_restart(bool, uint32_t) {}

// Whatever the GPS has sent since the last call, up to one complete fix.
// Runs while not recording too, so the UART buffer never fills up.
static bool next_fix(uint32_t, GpsFix *fix) {
    while (Serial1.available()) {
        if (nmea_feed(&nmea, (char)Serial1.read(), fix)) return true;
    }
    return false;
}

#endif

static const char *const STATE_NAMES[] = {"idle", "waking", "riding", "paused", "finished",
                                          "sleeping", "setup"};

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
        switch (button_update(&boot, digitalRead(BOOT_BUTTON_PIN) == LOW, now)) {
        case BUTTON_PRESS: in = UI_IN_PAGE; xQueueSend(inputs, &in, 0); break;
        case BUTTON_LONG:  in = UI_IN_PAGE_LONG; xQueueSend(inputs, &in, 0); break;
        case BUTTON_NONE:  break;
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
        sync_cancel();  // Wi-Fi off while riding
        ride_stats_init(&stats);
        ride_log_start(&ride_log, RIDES_DIR);
        fixes_restart(true, now);
        recording = true;
        break;
    case UI_ACT_PAUSE:
        ride_stats_pause(&stats);
        ride_log_pause(&ride_log);
        recording = false;
        break;
    case UI_ACT_RESUME:
        fixes_restart(false, now);
        recording = true;
        break;
    case UI_ACT_FINISH:
        recording = false;
        if (ride_log_finish(&ride_log)) {
            Serial.printf("# gpx: saved, %lu points\n", (unsigned long)ride_log.points);
        } else {
            Serial.printf("# gpx: not saved cleanly; %s is repaired at the next boot\n",
                          ride_log.path);
        }
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

// Mount the ride filesystem. Formats only a partition that was never used
// (still erased): one that fails to mount for another reason may hold rides.
static bool mount_storage() {
    if (LittleFS.begin(false)) return true;

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (!part) {
        Serial.println("# storage: no partition, check partitions.csv");
        return false;
    }
    // LittleFS keeps its superblock in the first two 4 KB blocks
    static uint8_t chunk[256];
    for (uint32_t at = 0; at < 2 * 4096; at += sizeof(chunk)) {
        if (esp_partition_read(part, at, chunk, sizeof(chunk)) != ESP_OK) return false;
        for (uint8_t b : chunk) {
            if (b != 0xFF) {
                Serial.println("# storage: mount failed and the partition isn't blank; "
                               "not formatting, rides may be on it. Rides won't be saved.");
                return false;
            }
        }
    }
    Serial.println("# storage: blank partition, formatting");
    return LittleFS.format() && LittleFS.begin(false);
}

// Repair rides cut off by a power cut, then list what's stored
static void check_rides() {
    int repaired = ride_log_recover(RIDES_DIR);
    if (repaired < 0) Serial.println("# rides: a cut-off ride couldn't be repaired (flash full?)");
    if (repaired > 0) Serial.printf("# rides: repaired %d cut-off ride(s)\n", repaired);

    File root = LittleFS.open("/");
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        Serial.printf("# rides: %s %u bytes\n", f.name(), (unsigned)f.size());
    }
    Serial.printf("# rides: %u of %u KB used\n", (unsigned)(LittleFS.usedBytes() / 1024),
                  (unsigned)(LittleFS.totalBytes() / 1024));
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
    fixes_begin();
    if (mount_storage()) check_rides();

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

    // Fixes without a position (no satellites yet) change nothing
    GpsFix fix;
    if (next_fix(now, &fix) && recording && fix.valid) {
        ride_stats_update(&stats, &fix);
        ui_ride_update(&ui, &stats, now);
        bool was_failed = ride_log.failed;
        ride_log_point(&ride_log, &fix);
        if (ride_log.failed && !was_failed) {
            Serial.println("# gpx: write failed (flash full?); the rest of this ride isn't saved");
        }
    }

    if (now - battery_read_ms >= BATTERY_EVERY_MS) {
        battery_read_ms = now;
        update_battery();
    }

    UiState before = ui.state;
    ui_tick(&ui, now);

    // Setup runs while the UI shows it. Wi-Fi does one thing at a time: a
    // sync in progress stops first.
    if (ui.state == UI_SETUP && !setup_open) {
        sync_cancel();
        if (!sync_running()) {
            wifi_setup_start();
            setup_open = true;
        }
    }
    if (setup_open) {
        const uint8_t *qr;
        uint8_t     size;
        const char *caption;
        switch (wifi_setup_poll()) {
        case SETUP_SCREEN:
            wifi_setup_screen(&qr, &size, &caption);
            ui_set_qr(&ui, qr, size, caption);
            break;
        case SETUP_DONE: ui_setup_done(&ui, now); break;
        case SETUP_NONE: break;
        }
    }
    if (ui.state != UI_SETUP && setup_open) {
        wifi_setup_stop();
        setup_open = false;
    }
    if (ui.state != before) Serial.printf("# %s\n", STATE_NAMES[ui.state]);

    // Back in idle (woken up, ride finished, setup closed): upload finished rides
    static UiState last = UI_WAKING;
    if (ui.state == UI_IDLE && last != UI_IDLE) sync_start();
    last = ui.state;

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
    // Sleep screen is on the panel; an upload in progress finishes first
    if (ui.shown.state == UI_SLEEPING && !sync_running()) sleep_now();

    delay(10);
}
