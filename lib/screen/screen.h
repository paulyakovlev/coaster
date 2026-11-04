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

// Side-view pixel rider. Every pose fills the same box, so changing pose is
// one partial refresh of that box.
#define RIDER_WIDTH  96
#define RIDER_HEIGHT 96

enum RiderPose {
    RIDER_STOPPED,  // stopped mid-ride: dozing on the bike
    RIDER_FAST,     // flattened by the wind, speed lines
    RIDER_CLIMBING, // nose up a slope, sweating
    RIDER_CRUISING, // riding on the flat
    RIDER_BED,      // idle: asleep in bed
    RIDER_WAKING,   // ride starting: sits up in bed
    RIDER_COFFEE,   // paused by hand: coffee next to the bike
    RIDER_COUCH,    // ride finished: on the couch
    RIDER_POSE_COUNT
};

// Frames per pose, for a little movement (see the refresh budget in SPEC.md)
#define RIDER_FRAMES 2

// Draw the rider with the box's top-left corner at (x, y). Clears the box first.
void screen_draw_rider(GFXcanvas1 &canvas, int16_t x, int16_t y, RiderPose pose, uint8_t frame);

// Same at any whole-number scale: 2 fills the 96x96 box, 3 is 144x144
void screen_draw_rider_scaled(GFXcanvas1 &canvas, int16_t x, int16_t y, RiderPose pose,
                              uint8_t frame, int16_t scale);

// The rider alone, centered, for previewing poses.
void screen_draw_rider_preview(GFXcanvas1 &canvas, RiderPose pose, uint8_t frame);

// A page: one big number with its unit underneath. With the rider, the
// number sits in the top half and the rider box at the bottom, at
// (RIDER_X, RIDER_Y). The number shrinks to fit long values like "10:05".
#define RIDER_X ((SCREEN_WIDTH - RIDER_WIDTH) / 2)
#define RIDER_Y (SCREEN_HEIGHT - RIDER_HEIGHT - 4)

void screen_draw_page(GFXcanvas1 &canvas, const char *value, const char *unit, bool show_rider,
                      RiderPose pose, uint8_t frame);

// The dashboard: one big number in the middle, two small ones in the top
// corners, each with its unit underneath. No rider.
void screen_draw_dashboard(GFXcanvas1 &canvas, const char *main, const char *main_unit,
                           const char *left, const char *left_unit, const char *right,
                           const char *right_unit);

// A scene: the rider large (3x), with a short caption underneath. For the
// moments without a number, like idle and waking up.
void screen_draw_scene(GFXcanvas1 &canvas, RiderPose pose, uint8_t frame, const char *caption);
