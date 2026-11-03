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

// Side-view pixel rider. Every pose fills the same box, so changing pose is
// one partial refresh of that box.
#define RIDER_WIDTH  96
#define RIDER_HEIGHT 96

enum RiderPose {
    RIDER_STOPPED,  // asleep, snoring
    RIDER_FAST,     // flattened by the wind, speed lines
    RIDER_CLIMBING, // nose up a slope, sweating
};

// Draw the rider with the box's top-left corner at (x, y). Clears the box first.
void screen_draw_rider(GFXcanvas1 &canvas, int16_t x, int16_t y, RiderPose pose);

// The rider alone, centered, for previewing poses.
void screen_draw_rider_preview(GFXcanvas1 &canvas, RiderPose pose);
