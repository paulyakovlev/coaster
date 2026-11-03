#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void set_state(Ui *ui, UiState state, uint32_t now_ms) {
    ui->state          = state;
    ui->state_since_ms = now_ms;
}

// Riding starts fresh: plain cruising, no grade history
static void reset_riding(Ui *ui, uint32_t now_ms) {
    ui->pose               = RIDER_CRUISING;
    ui->candidate          = RIDER_CRUISING;
    ui->candidate_since_ms = now_ms;
    ui->hist_len           = 0;
    ui->hist_head          = 0;
}

void ui_init(Ui *ui, uint32_t now_ms) {
    memset(ui, 0, sizeof(*ui));
    set_state(ui, UI_IDLE, now_ms);
    reset_riding(ui, now_ms);
}

static void next_page(Ui *ui) {
    ui->page = (UiPage)((ui->page + 1) % UI_PAGE_COUNT);
}

UiAction ui_input(Ui *ui, UiInput in, uint32_t now_ms) {
    switch (ui->state) {
    case UI_IDLE:
        if (in == UI_IN_PWR_LONG) {
            set_state(ui, UI_WAKING, now_ms);
            ui->page = UI_PAGE_RIDER;
            reset_riding(ui, now_ms);
            return UI_ACT_START;
        }
        break;

    case UI_WAKING:
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
            ui->page = UI_PAGE_RIDER;  // couch and total distance
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
    if (ui->state != UI_RIDING && ui->state != UI_WAKING) return;

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
    }
}

void ui_tick(Ui *ui, uint32_t now_ms) {
    if (ui->state == UI_WAKING && now_ms - ui->state_since_ms >= UI_WAKING_MS) {
        set_state(ui, UI_RIDING, now_ms);
    }
}

#if UI_IMPERIAL
#define SPEED_FACTOR    2.236936f   // m/s -> mph
#define SPEED_UNIT      "mph"
#define DISTANCE_FACTOR 1609.344    // m per mile
#define DISTANCE_UNIT   "mi"
#define CLIMB_FACTOR    3.28084f    // m -> ft
#define CLIMB_UNIT      "ft up"
#else
#define SPEED_FACTOR    3.6f
#define SPEED_UNIT      "km/h"
#define DISTANCE_FACTOR 1000.0
#define DISTANCE_UNIT   "km"
#define CLIMB_FACTOR    1.0f
#define CLIMB_UNIT      "m up"
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
    }
}

// Which values a page shows in a state. Unused slots are NO_VALUE.
#define NO_VALUE ((UiValue)-1)

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
        *main = UI_VAL_CLIMB;
        break;
    }
}

static void format_slot(UiValue v, const RideStats *stats, char *buf, const char **unit) {
    if (v == NO_VALUE) {
        buf[0] = '\0';
        *unit  = "";
        return;
    }
    ui_format(v, stats, buf, UI_VALUE_MAX, unit);
}

static RiderPose rest_pose(UiState state) {
    switch (state) {
    case UI_IDLE:   return RIDER_BED;
    case UI_WAKING: return RIDER_WAKING;
    case UI_PAUSED: return RIDER_COFFEE;
    default:        return RIDER_COUCH;
    }
}

static void build_frame(const Ui *ui, const RideStats *stats, UiFrame *f) {
    memset(f, 0, sizeof(*f));
    f->state   = ui->state;
    f->page    = ui->page;
    f->pose    = RIDER_POSE_COUNT;
    f->stopped = ui->state == UI_RIDING && ui->pose == RIDER_STOPPED;
    if (ui->state == UI_IDLE || ui->state == UI_WAKING) {
        f->pose = rest_pose(ui->state);
        return;
    }
    if (ui->page == UI_PAGE_RIDER) f->pose = ui->state == UI_RIDING ? ui->pose : rest_pose(ui->state);

    UiValue main, left, right;
    const char *unit;
    page_values(ui->page, ui->state, &main, &left, &right);
    format_slot(main, stats, f->main, &unit);
    format_slot(left, stats, f->left, &unit);
    format_slot(right, stats, f->right, &unit);
}

static bool same_frame(const UiFrame *a, const UiFrame *b) {
    return a->state == b->state && a->page == b->page && a->pose == b->pose &&
           a->stopped == b->stopped && strcmp(a->main, b->main) == 0 &&
           strcmp(a->left, b->left) == 0 && strcmp(a->right, b->right) == 0;
}

UiRefresh ui_render(Ui *ui, const RideStats *stats, GFXcanvas1 &canvas) {
    UiFrame f;
    build_frame(ui, stats, &f);
    if (ui->has_shown && same_frame(&f, &ui->shown)) return UI_REFRESH_NONE;

    // Full refreshes (the flash) go where they're expected anyway: entering a
    // rest state, waking up, and stopping mid-ride (the rider dozes off).
    // Plus a fallback every UI_FULL_EVERY partials.
    bool full = !ui->has_shown || ui->partials >= UI_FULL_EVERY ||
                (f.state != ui->shown.state && f.state != UI_RIDING) ||
                (f.stopped && !ui->shown.stopped);

    if (f.state == UI_IDLE) {
        screen_draw_scene(canvas, RIDER_BED, "hold PWR to ride");
    } else if (f.state == UI_WAKING) {
        screen_draw_scene(canvas, RIDER_WAKING, "let's ride!");
    } else {
        UiValue main, left, right;
        const char *main_unit, *left_unit, *right_unit;
        page_values(f.page, f.state, &main, &left, &right);
        char m[UI_VALUE_MAX], l[UI_VALUE_MAX], r[UI_VALUE_MAX];
        format_slot(main, stats, m, &main_unit);
        format_slot(left, stats, l, &left_unit);
        format_slot(right, stats, r, &right_unit);
        if (f.page == UI_PAGE_DASHBOARD) {
            screen_draw_dashboard(canvas, m, main_unit, l, left_unit, r, right_unit);
        } else {
            bool rider = f.pose != RIDER_POSE_COUNT;
            screen_draw_page(canvas, m, main_unit, rider, rider ? f.pose : RIDER_CRUISING);
        }
    }

    ui->shown     = f;
    ui->has_shown = true;
    ui->partials  = full ? 0 : ui->partials + 1;
    return full ? UI_REFRESH_FULL : UI_REFRESH_PARTIAL;
}
