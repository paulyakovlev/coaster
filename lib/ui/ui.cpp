#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// Riding poses: GPS fixes per animation frame. Cruising and fast pedal at
// 1 fps (the panel's limit); climbing is a slow grind; dozing slower still.
// Indexed by RiderPose: stopped, fast, climbing, cruising, then the rest
// poses (bed, waking, coffee), which have one frame and don't animate.
static const uint8_t ANIM_FIXES[RIDER_POSE_COUNT] = {2, 1, 2, 1, 0, 0, 0};

static bool moving(RiderPose pose) {
    return pose == RIDER_FAST || pose == RIDER_CLIMBING || pose == RIDER_CRUISING;
}

static void restart_animation(Ui *ui) {
    ui->frame          = 0;
    ui->fixes_on_frame = 0;
}

static void set_state(Ui *ui, UiState state, uint32_t now_ms) {
    ui->state          = state;
    ui->state_since_ms = now_ms;
    restart_animation(ui);
}

// Riding starts fresh: plain cruising, no grade history
static void reset_riding(Ui *ui, uint32_t now_ms) {
    ui->pose               = RIDER_CRUISING;
    ui->candidate          = RIDER_CRUISING;
    ui->candidate_since_ms = now_ms;
    ui->hist_len           = 0;
    ui->hist_head          = 0;
    ui->rider_x            = RIDER_X;
}

void ui_init(Ui *ui, uint32_t now_ms) {
    memset(ui, 0, sizeof(*ui));
    set_state(ui, UI_WAKING, now_ms);
    reset_riding(ui, now_ms);
    ui->battery_pct = -1;
}

static void next_page(Ui *ui) {
    ui->page = (UiPage)((ui->page + 1) % UI_PAGE_COUNT);
}

UiAction ui_input(Ui *ui, UiInput in, uint32_t now_ms) {
    // A slow press of BOOT still turns the page; only idle tells them apart
    if (in == UI_IN_PAGE_LONG && ui->state != UI_IDLE) in = UI_IN_PAGE;

    switch (ui->state) {
    case UI_IDLE:
        if (in == UI_IN_PAGE_LONG) {
            set_state(ui, UI_SETUP, now_ms);
            ui_set_qr(ui, nullptr, 0, "one moment");  // until the caller has a screen
        } else if (in == UI_IN_PWR_LONG) {
            set_state(ui, UI_RIDING, now_ms);
            ui->page = UI_PAGE_RIDER;
            reset_riding(ui, now_ms);
            return UI_ACT_START;
        }
        break;

    case UI_SETUP:
        set_state(ui, UI_IDLE, now_ms);  // any press leaves
        break;

    case UI_WAKING:
    case UI_SLEEPING:
        break;

    case UI_RIDING:
        if (in == UI_IN_PAGE) {
            next_page(ui);
        } else {
            // Short or long: either way the rider wants to stop
            set_state(ui, UI_PAUSED, now_ms);
            return UI_ACT_PAUSE;
        }
        break;

    case UI_PAUSED:
        if (in == UI_IN_PAGE) {
            next_page(ui);
        } else if (in == UI_IN_PWR) {
            set_state(ui, UI_RIDING, now_ms);
            reset_riding(ui, now_ms);
            return UI_ACT_RESUME;
        } else {
            set_state(ui, UI_FINISHED, now_ms);
            ui->page = UI_PAGE_RIDER;  // total distance
            return UI_ACT_FINISH;
        }
        break;

    case UI_FINISHED:
        if (in == UI_IN_PAGE) {
            next_page(ui);
        } else if (in == UI_IN_PWR_LONG) {
            set_state(ui, UI_IDLE, now_ms);
        }
        break;
    }
    return UI_ACT_NONE;
}

// Grade over the most recent stretch of at least UI_GRADE_MIN_M, or 0 if
// the history doesn't cover that much road yet
static float grade(const Ui *ui) {
    if (ui->hist_len < 2) return 0;
    int newest = (ui->hist_head + UI_GRADE_WINDOW - 1) % UI_GRADE_WINDOW;
    for (int back = 1; back < ui->hist_len; back++) {
        int i = (newest + UI_GRADE_WINDOW - back) % UI_GRADE_WINDOW;
        float run = ui->hist_dist[newest] - ui->hist_dist[i];
        if (run >= UI_GRADE_MIN_M) return (ui->hist_ele[newest] - ui->hist_ele[i]) / run;
    }
    return 0;
}

void ui_ride_update(Ui *ui, const RideStats *stats, uint32_t now_ms) {
    if (ui->state != UI_RIDING) return;

    ui->hist_dist[ui->hist_head] = (float)stats->distance_m;
    ui->hist_ele[ui->hist_head]  = stats->ele_m;
    ui->hist_head = (ui->hist_head + 1) % UI_GRADE_WINDOW;
    if (ui->hist_len < UI_GRADE_WINDOW) ui->hist_len++;

    RiderPose want;
    if (stats->speed_mps < RIDE_MOVING_MPS) {
        want = RIDER_STOPPED;
    } else if (stats->speed_mps >= UI_FAST_MPS) {
        want = RIDER_FAST;
    } else if (grade(ui) >= UI_CLIMB_GRADE) {
        want = RIDER_CLIMBING;
    } else {
        want = RIDER_CRUISING;
    }

    if (want != ui->candidate) {
        ui->candidate          = want;
        ui->candidate_since_ms = now_ms;
    }
    uint32_t hold = ui->candidate == RIDER_STOPPED ? UI_STOPPED_HOLD_MS : UI_POSE_HOLD_MS;
    if (ui->candidate != ui->pose && now_ms - ui->candidate_since_ms >= hold) {
        ui->pose = ui->candidate;
        restart_animation(ui);
        if (!moving(ui->pose)) ui->rider_x = RIDER_X;  // moves on from the middle
        return;
    }

    if (++ui->fixes_on_frame >= ANIM_FIXES[ui->pose]) {
        ui->frame          = (ui->frame + 1) % RIDER_FRAMES;
        ui->fixes_on_frame = 0;
        if (moving(ui->pose)) {
            ui->rider_x -= UI_RIDER_STEP;
            // Gone off the left edge: back in from the right
            if (ui->rider_x <= -RIDER_WIDTH) ui->rider_x = SCREEN_WIDTH - UI_RIDER_STEP;
        }
    }
}

void ui_set_qr(Ui *ui, const uint8_t *qr, uint8_t size, const char *caption) {
    ui->qr         = qr;
    ui->qr_size    = qr ? size : 0;
    ui->qr_caption = caption;
    ui->qr_version++;
}

void ui_setup_done(Ui *ui, uint32_t now_ms) {
    if (ui->state == UI_SETUP) set_state(ui, UI_IDLE, now_ms);
}

void ui_tick(Ui *ui, uint32_t now_ms) {
    if (ui->state == UI_SETUP && now_ms - ui->state_since_ms >= UI_SETUP_MS) {
        set_state(ui, UI_IDLE, now_ms);
    }
    if (ui->state == UI_WAKING && now_ms - ui->state_since_ms >= UI_WAKING_MS) {
        set_state(ui, UI_IDLE, now_ms);
    }
    if (ui->state == UI_IDLE && now_ms - ui->state_since_ms >= UI_SLEEP_AFTER_MS) {
        set_state(ui, UI_SLEEPING, now_ms);
    }
}

#if UI_IMPERIAL
#define SPEED_FACTOR    2.236936f   // m/s -> mph
#define SPEED_UNIT      "mph"
#define DISTANCE_FACTOR 1609.344    // m per mile
#define DISTANCE_UNIT   "mi"
#define CLIMB_FACTOR    3.28084f    // m -> ft
#define CLIMB_UNIT      "ft up"
#define ELEVATION_UNIT  "ft alt"
#else
#define SPEED_FACTOR    3.6f
#define SPEED_UNIT      "km/h"
#define DISTANCE_FACTOR 1000.0
#define DISTANCE_UNIT   "km"
#define CLIMB_FACTOR    1.0f
#define CLIMB_UNIT      "m up"
#define ELEVATION_UNIT  "m alt"
#endif

void ui_format(UiValue v, const RideStats *stats, char *buf, size_t size, const char **unit) {
    switch (v) {
    case UI_VAL_SPEED:
        snprintf(buf, size, "%ld", lroundf(stats->speed_mps * SPEED_FACTOR));
        *unit = SPEED_UNIT;
        break;

    case UI_VAL_AVG_SPEED:
        snprintf(buf, size, "%ld", lroundf(ride_stats_avg_speed_mps(stats) * SPEED_FACTOR));
        *unit = "avg " SPEED_UNIT;
        break;

    case UI_VAL_DISTANCE: {
        // One decimal below 100; it changes every 0.1 mi (or km)
        double d = stats->distance_m / DISTANCE_FACTOR;
        snprintf(buf, size, d < 99.95 ? "%.1f" : "%.0f", d);
        *unit = DISTANCE_UNIT;
        break;
    }

    case UI_VAL_TIME: {
        // Hours and minutes only: changes once a minute, not every second
        uint32_t min = ride_stats_elapsed_s(stats) / 60;
        snprintf(buf, size, "%lu:%02lu", (unsigned long)(min / 60), (unsigned long)(min % 60));
        *unit = "h:mm";
        break;
    }

    case UI_VAL_CLIMB:
        snprintf(buf, size, "%ld", lroundf(stats->ele_gain_m * CLIMB_FACTOR));
        *unit = CLIMB_UNIT;
        break;

    case UI_VAL_ELEVATION:
        snprintf(buf, size, "%ld", lroundf(stats->ele_m * CLIMB_FACTOR));
        *unit = ELEVATION_UNIT;
        break;
    }
}

// Which values a page shows in a state. Unused slots are NO_VALUE. The
// battery isn't a ride value, so it gets its own marker.
#define NO_VALUE      ((UiValue)-1)
#define BATTERY_VALUE ((UiValue)-2)

static void page_values(UiPage page, UiState state, UiValue *main, UiValue *left, UiValue *right) {
    *left = *right = NO_VALUE;
    switch (page) {
    case UI_PAGE_RIDER:
        *main = state == UI_RIDING ? UI_VAL_SPEED : UI_VAL_DISTANCE;
        break;
    case UI_PAGE_DASHBOARD:
        *main  = state == UI_FINISHED ? UI_VAL_AVG_SPEED : UI_VAL_SPEED;
        *left  = UI_VAL_DISTANCE;
        *right = UI_VAL_TIME;
        break;
    case UI_PAGE_ELEVATION:
    default:
        *main  = UI_VAL_CLIMB;
        *left  = UI_VAL_ELEVATION;
        *right = BATTERY_VALUE;
        break;
    }
}

static void format_slot(const Ui *ui, UiValue v, const RideStats *stats, char *buf,
                        const char **unit) {
    if (v == BATTERY_VALUE) {
        if (ui->battery_pct < 0) {
            snprintf(buf, UI_VALUE_MAX, "--");
        } else {
            snprintf(buf, UI_VALUE_MAX, "%d%%", ui->battery_pct);
        }
        *unit = "battery";
        return;
    }
    if (v == NO_VALUE) {
        buf[0] = '\0';
        *unit  = "";
        return;
    }
    ui_format(v, stats, buf, UI_VALUE_MAX, unit);
}

// Rider page outside riding. Finished has no rider, just the distance.
static RiderPose rest_pose(UiState state) {
    switch (state) {
    case UI_SLEEPING: return RIDER_BED;
    case UI_WAKING:   return RIDER_WAKING;
    case UI_IDLE:
    case UI_PAUSED:   return RIDER_COFFEE;
    default:          return RIDER_POSE_COUNT;
    }
}

static void build_frame(const Ui *ui, const RideStats *stats, UiFrame *f) {
    memset(f, 0, sizeof(*f));
    f->state   = ui->state;
    f->page    = ui->page;
    f->pose    = RIDER_POSE_COUNT;
    f->stopped = ui->state == UI_RIDING && ui->pose == RIDER_STOPPED;
    f->battery = -1;
    if (ui->state == UI_IDLE) f->battery = ui->battery_pct;
    if (ui->state == UI_SETUP) {
        f->qr = ui->qr_version;
        return;
    }
    if (ui->state == UI_IDLE || ui->state == UI_WAKING || ui->state == UI_SLEEPING) {
        f->pose  = rest_pose(ui->state);
        f->frame = ui->frame;
        return;
    }
    if (ui->page == UI_PAGE_RIDER) {
        f->pose    = ui->state == UI_RIDING ? ui->pose : rest_pose(ui->state);
        f->frame   = ui->frame;
        f->rider_x = moving(f->pose) ? ui->rider_x : RIDER_X;
    }

    UiValue main, left, right;
    const char *unit;
    page_values(ui->page, ui->state, &main, &left, &right);
    format_slot(ui, main, stats, f->main, &unit);
    format_slot(ui, left, stats, f->left, &unit);
    format_slot(ui, right, stats, f->right, &unit);
}

static bool same_frame(const UiFrame *a, const UiFrame *b) {
    return a->state == b->state && a->page == b->page && a->pose == b->pose &&
           a->frame == b->frame && a->rider_x == b->rider_x && a->stopped == b->stopped && a->battery == b->battery && a->qr == b->qr &&
           strcmp(a->main, b->main) == 0 &&
           strcmp(a->left, b->left) == 0 && strcmp(a->right, b->right) == 0;
}

UiRefresh ui_render(Ui *ui, const RideStats *stats, GFXcanvas1 &canvas) {
    UiFrame f;
    build_frame(ui, stats, &f);
    if (ui->has_shown && same_frame(&f, &ui->shown)) return UI_REFRESH_NONE;

    // Full refreshes (the flash) go where they're expected anyway: state
    // changes and stopping mid-ride (the rider dozes off). Not on resuming a
    // ride, or waking -> idle, right after the full refresh at boot. Plus a
    // fallback every UI_FULL_EVERY partials.
    bool quiet_change = ui->shown.state == UI_WAKING ||
                        (ui->shown.state == UI_PAUSED && f.state == UI_RIDING);
    bool full = !ui->has_shown || ui->partials >= UI_FULL_EVERY ||
                (f.state != ui->shown.state && !quiet_change) ||
                (f.stopped && !ui->shown.stopped) || f.qr != ui->shown.qr;

    char battery[5] = "";
    if (f.battery >= 0) snprintf(battery, sizeof(battery), "%d%%", f.battery);

    if (f.state == UI_SETUP) {
        screen_draw_qr(canvas, ui->qr, ui->qr_size, ui->qr_caption ? ui->qr_caption : "");
    } else if (f.state == UI_IDLE) {
        screen_draw_scene(canvas, RIDER_COFFEE, f.frame, "hold PWR to ride", battery);
    } else if (f.state == UI_SLEEPING) {
        screen_draw_scene(canvas, RIDER_BED, f.frame, "press PWR", "");
    } else if (f.state == UI_WAKING) {
        screen_draw_scene(canvas, RIDER_WAKING, f.frame, "good morning", "");
    } else {
        UiValue main, left, right;
        const char *main_unit, *left_unit, *right_unit;
        page_values(f.page, f.state, &main, &left, &right);
        char m[UI_VALUE_MAX], l[UI_VALUE_MAX], r[UI_VALUE_MAX];
        format_slot(ui, main, stats, m, &main_unit);
        format_slot(ui, left, stats, l, &left_unit);
        format_slot(ui, right, stats, r, &right_unit);
        if (left != NO_VALUE) {  // corner numbers: dashboard and elevation
            screen_draw_dashboard(canvas, m, main_unit, l, left_unit, r, right_unit);
        } else {
            bool rider = f.pose != RIDER_POSE_COUNT;
            screen_draw_page(canvas, m, main_unit, rider, rider ? f.pose : RIDER_CRUISING, f.frame,
                             f.rider_x);
        }
    }

    ui->shown     = f;
    ui->has_shown = true;
    ui->partials  = full ? 0 : ui->partials + 1;
    return full ? UI_REFRESH_FULL : UI_REFRESH_PARTIAL;
}
