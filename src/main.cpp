#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "epaper_driver_bsp.h"
#include "board_power_bsp.h"
#include "screen.h"

// Pin assignments — identical for V1 and V2 hardware
// Source: vendor/ESP32-S3-ePaper-1.54/02_Example/Arduino/*/user_config.h
#define EPD_DC_PIN     10
#define EPD_CS_PIN     11
#define EPD_SCK_PIN    12
#define EPD_MOSI_PIN   13
#define EPD_RST_PIN    9
#define EPD_BUSY_PIN   8

#define EPD_PWR_PIN    6
#define AUDIO_PWR_PIN  42
#define VBAT_PWR_PIN   17

static board_power_bsp_t     power(EPD_PWR_PIN, AUDIO_PWR_PIN, VBAT_PWR_PIN);
static epaper_driver_display *driver = nullptr;

// Copy a canvas into the epaper buffer — only mark black pixels
static void blit(GFXcanvas1 &canvas) {
    driver->EPD_Clear();  // fill internal buffer with white (0xff)
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            if (canvas.getPixel(x, y) == SCREEN_BLACK) {
                driver->EPD_DrawColorPixel(x, y, DRIVER_COLOR_BLACK);
            }
        }
    }
}

void setup() {
    Serial.begin(115200);

    // Power on the EPD rail before touching SPI
    power.VBAT_POWER_ON();
    power.POWEER_EPD_ON();
    delay(20);

    // Build driver config from pin definitions
    custom_lcd_spi_t cfg = {};
    cfg.cs         = EPD_CS_PIN;
    cfg.dc         = EPD_DC_PIN;
    cfg.rst        = EPD_RST_PIN;
    cfg.busy       = EPD_BUSY_PIN;
    cfg.mosi       = EPD_MOSI_PIN;
    cfg.scl        = EPD_SCK_PIN;
    cfg.spi_host   = SPI2_HOST;
    cfg.buffer_len = SCREEN_WIDTH * SCREEN_HEIGHT / 8;  // 5000 bytes, allocated in SPIRAM

    driver = new epaper_driver_display(SCREEN_WIDTH, SCREEN_HEIGHT, cfg);
    driver->EPD_Init();

    // Off-screen 1-bit canvas (allocates in internal RAM)
    GFXcanvas1 canvas(SCREEN_WIDTH, SCREEN_HEIGHT);
    screen_draw_hello(canvas);
    blit(canvas);

    driver->EPD_Display();
    // E-paper holds the image indefinitely without power
}

void loop() {}
