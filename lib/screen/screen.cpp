#include "screen.h"

#include <math.h>
#include <stdio.h>

// Print text horizontally centered, with its top edge at y.
static void print_centered(GFXcanvas1 &canvas, const char *text, int16_t y) {
    int16_t tx, ty;
    uint16_t tw, th;
    canvas.getTextBounds(text, 0, 0, &tx, &ty, &tw, &th);
    canvas.setCursor((SCREEN_WIDTH - tw) / 2 - tx, y - ty);
    canvas.print(text);
}

void screen_draw_hello(GFXcanvas1 &canvas) {
    canvas.fillScreen(SCREEN_WHITE);
    canvas.setTextColor(SCREEN_BLACK);
    canvas.setTextWrap(false);
    canvas.setTextSize(2);

    // Center vertically too: 7 px glyph height at size 2
    print_centered(canvas, "hello rider.", (SCREEN_HEIGHT - 14) / 2);
}

void screen_draw_speed(GFXcanvas1 &canvas, float speed_kmh) {
    canvas.fillScreen(SCREEN_WHITE);
    canvas.setTextColor(SCREEN_BLACK);
    canvas.setTextWrap(false);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)lroundf(speed_kmh));

    // Built-in 5x7 font scaled 10x: 70 px tall digits
    canvas.setTextSize(10);
    print_centered(canvas, buf, 40);

    canvas.setTextSize(2);
    print_centered(canvas, "km/h", 130);
}
