#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "display.h"
#include "screen.h"

void setup() {
    Serial.begin(115200);

    // Off-screen 1-bit canvas (allocates in internal RAM)
    GFXcanvas1 canvas(SCREEN_WIDTH, SCREEN_HEIGHT);
    screen_draw_hello(canvas);
    display_begin(canvas);
    // E-paper holds the image indefinitely without power
}

void loop() {}
