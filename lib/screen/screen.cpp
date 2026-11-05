#include "screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// Print text horizontally centered, with its top edge at y.
static void print_centered(GFXcanvas1 &canvas, const char *text, int16_t y) {
    int16_t tx, ty;
    uint16_t tw, th;
    canvas.getTextBounds(text, 0, 0, &tx, &ty, &tw, &th);
    canvas.setCursor((SCREEN_WIDTH - tw) / 2 - tx, y - ty);
    canvas.print(text);
}

// Numbers use the built-in font too, but "." and ":" get 5 columns instead
// of 6, so "12.4" and "1:07" fit the dashboard corners at 4x. Both glyphs
// only use columns 1-2, which leaves 2 columns of space on either side.
static bool narrow(char c) {
    return c == '.' || c == ':';
}

static int16_t number_width(const char *text, uint8_t size) {
    int16_t w = 0;
    for (const char *c = text; *c; c++) w += (narrow(*c) ? 5 : 6) * size;
    return w > 0 ? w - size : 0;  // no trailing gap after the last character
}

// Print a number with its top edge at y and its left edge at x
static void print_number(GFXcanvas1 &canvas, const char *text, int16_t x, int16_t y,
                         uint8_t size) {
    for (const char *c = text; *c; c++) {
        canvas.drawChar(x, y, *c, SCREEN_BLACK, SCREEN_BLACK, size);
        x += (narrow(*c) ? 5 : 6) * size;
    }
}

// Largest size, up to max_size, at which a number fits in width pixels
static uint8_t fit_number(const char *text, uint8_t max_size, int16_t width = SCREEN_WIDTH - 8) {
    for (uint8_t size = max_size; size > 1; size--) {
        if (number_width(text, size) <= width) return size;
    }
    return 1;
}

static void print_number_centered(GFXcanvas1 &canvas, const char *text, int16_t y, uint8_t size) {
    print_number(canvas, text, (SCREEN_WIDTH - number_width(text, size)) / 2, y, size);
}

// Largest text size, up to max_size, at which text fits in width pixels.
// Built-in font: 6 px per character at size 1, 7 px tall.
static uint8_t fit_size(const char *text, uint8_t max_size, int16_t width = SCREEN_WIDTH - 8) {
    int16_t len = (int16_t)strlen(text);
    for (uint8_t size = max_size; size > 1; size--) {
        if (len * 6 * size - size <= width) return size;
    }
    return 1;
}

// Units under the numbers: 3x, 21 px tall
#define UNIT_SIZE 3

void screen_draw_page(GFXcanvas1 &canvas, const char *value, const char *unit, bool show_rider,
                      RiderPose pose, uint8_t frame) {
    canvas.fillScreen(SCREEN_WHITE);
    canvas.setTextColor(SCREEN_BLACK);
    canvas.setTextWrap(false);

    if (!show_rider) {
        // Up to 10x: 70 px tall digits, centered in the space above the unit
        uint8_t size = fit_number(value, 10);
        print_number_centered(canvas, value, 75 - 7 * size / 2, size);
        canvas.setTextSize(UNIT_SIZE);
        print_centered(canvas, unit, 128);
        return;
    }

    // Up to 8x: 56 px tall digits, unit just under, rider below that
    uint8_t size = fit_number(value, 8);
    print_number_centered(canvas, value, 36 - 7 * size / 2, size);
    canvas.setTextSize(UNIT_SIZE);
    print_centered(canvas, unit, 70);
    screen_draw_rider(canvas, RIDER_X, RIDER_Y, pose, frame);
}

// Print text with its top edge at y, left-aligned at x or right-aligned to x
static void print_at(GFXcanvas1 &canvas, const char *text, int16_t x, int16_t y, bool right) {
    int16_t tx, ty;
    uint16_t tw, th;
    canvas.getTextBounds(text, 0, 0, &tx, &ty, &tw, &th);
    canvas.setCursor((right ? x - (int16_t)tw : x) - tx, y - ty);
    canvas.print(text);
}

void screen_draw_dashboard(GFXcanvas1 &canvas, const char *main, const char *main_unit,
                           const char *left, const char *left_unit, const char *right,
                           const char *right_unit) {
    canvas.fillScreen(SCREEN_WHITE);
    canvas.setTextColor(SCREEN_BLACK);
    canvas.setTextWrap(false);

    // Corners: up to 4x numbers (28 px tall), each in its half of the width.
    // "12.4" and "1:07" fit at 4x; "10:05" drops to 3x. A thin divider keeps
    // them from reading as one number. 2x units under them.
    const int16_t margin = 4;
    const int16_t half   = SCREEN_WIDTH / 2 - margin - 8;
    canvas.drawFastVLine(SCREEN_WIDTH / 2, margin, 48, SCREEN_BLACK);
    uint8_t ls = fit_number(left, 4, half);
    uint8_t rs = fit_number(right, 4, half);
    print_number(canvas, left, margin, margin, ls);
    print_number(canvas, right, SCREEN_WIDTH - margin - number_width(right, rs), margin, rs);
    canvas.setTextSize(2);
    print_at(canvas, left_unit, margin, margin + 32, false);
    print_at(canvas, right_unit, SCREEN_WIDTH - margin, margin + 32, true);

    // Middle: as big as fits, up to 9x (63 px), centered below the corners
    uint8_t size = fit_number(main, 9);
    print_number_centered(canvas, main, 110 - 7 * size / 2, size);
    canvas.setTextSize(UNIT_SIZE);
    print_centered(canvas, main_unit, 160);
}

void screen_draw_scene(GFXcanvas1 &canvas, RiderPose pose, uint8_t frame, const char *caption,
                       const char *corner) {
    canvas.fillScreen(SCREEN_WHITE);
    const int16_t scale = 3;
    screen_draw_rider_scaled(canvas, (SCREEN_WIDTH - 48 * scale) / 2, 8, pose, frame, scale);

    canvas.setTextColor(SCREEN_BLACK);
    canvas.setTextWrap(false);
    canvas.setTextSize(fit_size(caption, 2));
    print_centered(canvas, caption, 172);

    // Small text in the top-right corner, clear of the rider art (battery %)
    canvas.setTextSize(2);
    canvas.setCursor(SCREEN_WIDTH - (int16_t)strlen(corner) * 12 - 2, 2);
    canvas.print(corner);
}
