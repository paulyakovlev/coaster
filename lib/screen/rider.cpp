#include "screen.h"

// Side-view rider: a silly blob on a bike, riding to the right.
// Pixel art, one char per sprite pixel: '#' is ink, '.' is paper. Every pose
// fills the whole rider box, extras (snore, speed lines) included, so a pose
// change redraws exactly that box.
//
// Outlines only, no large black fills: moving black areas ghost the most.
// Riding poses have two frames for a little movement; they should differ in as few
// pixels as possible, since every changed pixel flashes on e-paper.

#define RIDER_COLS   48
#define RIDER_ROWS   48
#define RIDER_SCALE  (RIDER_WIDTH / RIDER_COLS)

// Hand-drawn poses, converted from art/rider/ by scripts/rider_art.py
#include "rider_art.h"

// Indexed by RiderPose, then frame
static const char *const *const SPRITES[RIDER_POSE_COUNT][RIDER_FRAMES] = {
    {STOPPED, STOPPED_2}, {FAST, FAST_2},     {CLIMBING, CLIMBING_2}, {CRUISING, CRUISING_2},
    {BED, BED_2},         {WAKING, WAKING_2}, {COFFEE, COFFEE_2},
};

void screen_draw_rider_scaled(GFXcanvas1 &canvas, int16_t x, int16_t y, RiderPose pose,
                              uint8_t frame, int16_t scale) {
    canvas.fillRect(x, y, RIDER_COLS * scale, RIDER_ROWS * scale, SCREEN_WHITE);

    const char *const *rows = SPRITES[pose][frame % RIDER_FRAMES];
    for (int16_t r = 0; r < RIDER_ROWS; r++) {
        for (int16_t c = 0; c < RIDER_COLS; c++) {
            if (rows[r][c] == '#') {
                canvas.fillRect(x + c * scale, y + r * scale, scale, scale, SCREEN_BLACK);
            }
        }
    }
}

void screen_draw_rider(GFXcanvas1 &canvas, int16_t x, int16_t y, RiderPose pose, uint8_t frame) {
    screen_draw_rider_scaled(canvas, x, y, pose, frame, RIDER_SCALE);
}

void screen_draw_rider_preview(GFXcanvas1 &canvas, RiderPose pose, uint8_t frame) {
    canvas.fillScreen(SCREEN_WHITE);
    screen_draw_rider(canvas, (SCREEN_WIDTH - RIDER_WIDTH) / 2,
                      (SCREEN_HEIGHT - RIDER_HEIGHT) / 2, pose, frame);
}
