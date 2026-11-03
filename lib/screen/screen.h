#pragma once

#include <Adafruit_GFX.h>

// Screen drawing, independent of the display hardware. The firmware sends
// the canvas to the e-paper panel; the simulator in sim/ saves it as a PNG.
//
// Canvas colors: 1 = white (paper), 0 = black (ink).

#define SCREEN_WIDTH  200
#define SCREEN_HEIGHT 200

#define SCREEN_WHITE  1
#define SCREEN_BLACK  0

void screen_draw_hello(GFXcanvas1 &canvas);

// Placeholder layout: one big number with its unit underneath.
void screen_draw_speed(GFXcanvas1 &canvas, float speed_kmh);
