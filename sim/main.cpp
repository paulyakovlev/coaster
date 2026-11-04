// Screen simulator: draws each screen with the same code as the firmware and
// saves it as a PNG, so layouts can be checked without the board.
//
// Run: pio run -e native -t exec   (writes to sim_out/)

#include <stdio.h>
#include <sys/stat.h>

#include <vector>

#include "png.h"
#include "fake_ride.h"
#include "ride.h"
#include "screen.h"
#include "ui.h"

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

static const char *const POSE_NAMES[RIDER_POSE_COUNT] = {
    "stopped", "fast", "climbing", "cruising", "bed", "waking", "coffee", "couch"};
static const char *const STATE_NAMES[] = {"idle", "waking", "riding", "paused", "finished"};
static const char *const PAGE_NAMES[]  = {"rider", "dashboard", "elevation"};

// Button presses in the scripted session, by second
static const struct {
    uint32_t t;
    UiInput  in;
} SCRIPT[] = {
    {5, UI_IN_PWR_LONG},                                   // start
    {40, UI_IN_PAGE}, {100, UI_IN_PAGE},                   // dashboard, elevation
    {130, UI_IN_PAGE},                                     // back to the rider
    {240, UI_IN_PWR}, {255, UI_IN_PAGE}, {270, UI_IN_PWR}, // pause, page, resume
    {320, UI_IN_PWR}, {330, UI_IN_PWR_LONG},               // pause, finish
    {340, UI_IN_PAGE}, {345, UI_IN_PAGE}, {350, UI_IN_PAGE},  // summary pages
    {360, UI_IN_PWR_LONG},                                 // back to bed
};

// Drive the real UI with the fake ride and the script. Saves full refreshes,
// frames right after a button press, and pose changes to sim_out/session/,
// and counts refreshes.
static void session(GFXcanvas1 &canvas) {
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/session", out_dir);
    mkdir(dir, 0755);

    Ui        ui;
    RideStats stats;
    FakeRide  ride;
    bool      recording = false;
    ride_stats_init(&stats);
    ui_init(&ui, 0);

    int counts[3] = {0, 0, 0};
    int by_state[5][3] = {};  // [UiState][UiRefresh]
    RiderPose last_pose = RIDER_POSE_COUNT;
    const uint32_t end_s = 370;

    for (uint32_t t = 0; t <= end_s; t++) {
        uint32_t now = t * 1000;
        bool pressed = false;
        for (const auto &step : SCRIPT) {
            if (step.t != t) continue;
            pressed = true;
            switch (ui_input(&ui, step.in, now)) {
            case UI_ACT_START:
                ride_stats_init(&stats);
                fake_ride_init(&ride, 1793690142u);
                recording = true;
                break;
            case UI_ACT_PAUSE:
                ride_stats_pause(&stats);
                recording = false;
                break;
            case UI_ACT_RESUME: recording = true; break;
            case UI_ACT_FINISH: recording = false; break;
            case UI_ACT_NONE: break;
            }
        }

        if (recording) {
            GpsFix fix;
            fake_ride_next(&ride, &fix);
            ride_stats_update(&stats, &fix);
            ui_ride_update(&ui, &stats, now);
        }
        ui_tick(&ui, now);

        UiRefresh r = ui_render(&ui, &stats, canvas);
        counts[r]++;
        by_state[ui.state][r]++;
        bool pose_changed = ui.shown.pose != last_pose;
        last_pose = ui.shown.pose;
        if (r == UI_REFRESH_FULL || (r != UI_REFRESH_NONE && (pressed || pose_changed))) {
            char name[96];
            snprintf(name, sizeof(name), "session/%03u_%s_%s_%s_%s", t, STATE_NAMES[ui.state],
                     PAGE_NAMES[ui.page], ui.shown.pose < RIDER_POSE_COUNT ? POSE_NAMES[ui.shown.pose] : "norider",
                     r == UI_REFRESH_FULL ? "full" : "partial");
            save(canvas, name);
        }
    }
    printf("session: %u s, %d full refreshes, %d partial, %d skipped (nothing changed)\n", end_s + 1,
           counts[UI_REFRESH_FULL], counts[UI_REFRESH_PARTIAL], counts[UI_REFRESH_NONE]);
    for (int st = 0; st < 5; st++) {
        int total = by_state[st][0] + by_state[st][1] + by_state[st][2];
        printf("  %-8s %3d s: %2d full, %3d partial, %3d skipped\n", STATE_NAMES[st], total,
               by_state[st][UI_REFRESH_FULL], by_state[st][UI_REFRESH_PARTIAL],
               by_state[st][UI_REFRESH_NONE]);
    }
}

int main(int argc, char **argv) {
    if (argc > 1) out_dir = argv[1];
    mkdir(out_dir, 0755);

    GFXcanvas1 canvas(SCREEN_WIDTH, SCREEN_HEIGHT);

    for (int pose = 0; pose < RIDER_POSE_COUNT; pose++) {
        for (uint8_t frame = 0; frame < RIDER_FRAMES; frame++) {
            char name[32];
            snprintf(name, sizeof(name), "rider_%s_%u", POSE_NAMES[pose], frame + 1);
            screen_draw_rider_preview(canvas, (RiderPose)pose, frame);
            save(canvas, name);
        }
    }

    // Longest values each page can show
    screen_draw_page(canvas, "18", "mph", true, RIDER_CRUISING, 0);
    save(canvas, "page_rider");
    screen_draw_dashboard(canvas, "18", "mph", "12.4", "mi", "1:07", "h:mm");
    save(canvas, "page_dashboard");
    screen_draw_dashboard(canvas, "8", "mph", "123", "mi", "10:05", "h:mm");
    save(canvas, "page_dashboard_long");
    screen_draw_dashboard(canvas, "14", "avg mph", "31.2", "mi", "2:15", "h:mm");
    save(canvas, "page_dashboard_summary");
    screen_draw_page(canvas, "1234", "ft up", false, RIDER_CRUISING, 0);
    save(canvas, "page_elevation");

    session(canvas);
    return 0;
}
