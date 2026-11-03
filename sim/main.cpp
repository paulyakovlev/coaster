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

    // Each speed with a pose that could go with it, with and without rider
    const struct {
        float speed;
        RiderPose pose;
    } rides[] = {{0, RIDER_STOPPED}, {8.4f, RIDER_CLIMBING}, {23.6f, RIDER_FAST}, {47, RIDER_FAST}};
    for (const auto &ride : rides) {
        char name[32];
        snprintf(name, sizeof(name), "speed_%ld", lroundf(ride.speed));
        screen_draw_speed(canvas, ride.speed, false, ride.pose);
        save(canvas, name);
        snprintf(name, sizeof(name), "speed_%ld_rider", lroundf(ride.speed));
        screen_draw_speed(canvas, ride.speed, true, ride.pose);
        save(canvas, name);
    }

    screen_draw_rider_preview(canvas, RIDER_STOPPED);
    save(canvas, "rider_stopped");
    screen_draw_rider_preview(canvas, RIDER_FAST);
    save(canvas, "rider_fast");
    screen_draw_rider_preview(canvas, RIDER_CLIMBING);
    save(canvas, "rider_climbing");
    return 0;
}
