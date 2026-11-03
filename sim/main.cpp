// Screen simulator: draws each screen with the same code as the firmware and
// saves it as a PNG, so layouts can be checked without the board.
//
// Run: pio run -e native -t exec   (writes to sim_out/)

#include <stdio.h>
#include <sys/stat.h>

#include <vector>

#include "png.h"
#include "screen.h"

#define SCALE 3  // 200x200 is tiny on a laptop screen

// Approximate e-paper tones; real paper isn't pure white
#define PAPER 0xE6
#define INK   0x1E

static const char *out_dir = "sim_out";

static void save(GFXcanvas1 &canvas, const char *name) {
    int w = SCREEN_WIDTH * SCALE, h = SCREEN_HEIGHT * SCALE;
    std::vector<uint8_t> pixels((size_t)w * h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            bool white = canvas.getPixel(x / SCALE, y / SCALE) == SCREEN_WHITE;
            pixels[(size_t)y * w + x] = white ? PAPER : INK;
        }
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/%s.png", out_dir, name);
    if (png_write_gray(path, pixels.data(), w, h)) {
        printf("wrote %s\n", path);
    } else {
        fprintf(stderr, "failed to write %s\n", path);
    }
}

int main(int argc, char **argv) {
    if (argc > 1) out_dir = argv[1];
    mkdir(out_dir, 0755);

    GFXcanvas1 canvas(SCREEN_WIDTH, SCREEN_HEIGHT);

    screen_draw_hello(canvas);
    save(canvas, "hello");

    const float speeds[] = {0, 8.4f, 23.6f, 47};
    for (float speed : speeds) {
        char name[32];
        snprintf(name, sizeof(name), "speed_%ld", lroundf(speed));
        screen_draw_speed(canvas, speed);
        save(canvas, name);
    }
    return 0;
}
