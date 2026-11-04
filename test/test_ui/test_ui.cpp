// Unit tests for lib/ui: buttons, states, pages, rider pose, refreshes.
// Run: pio test -e native

#include <string.h>
#include <unity.h>

#include "button.h"
#include "ui.h"

void setUp() {}
void tearDown() {}

// ---- Button -------------------------------------------------------------------

// Hold the level from t0 to t1 (exclusive), polling every 10 ms. Returns the
// first event seen, or BUTTON_NONE.
static ButtonEvent hold(Button *b, bool pressed, uint32_t t0, uint32_t t1) {
    ButtonEvent first = BUTTON_NONE;
    for (uint32_t t = t0; t < t1; t += 10) {
        ButtonEvent e = button_update(b, pressed, t);
        if (e != BUTTON_NONE && first == BUTTON_NONE) first = e;
    }
    return first;
}

static void test_button_short_press(void) {
    Button b;
    button_init(&b, false, 0);
    TEST_ASSERT_EQUAL(BUTTON_NONE, hold(&b, true, 0, 200));
    TEST_ASSERT_EQUAL(BUTTON_PRESS, hold(&b, false, 200, 300));
}

static void test_button_long_press_fires_while_held(void) {
    Button b;
    button_init(&b, false, 0);
    TEST_ASSERT_EQUAL(BUTTON_LONG, hold(&b, true, 0, 1100));
    TEST_ASSERT_EQUAL(BUTTON_NONE, hold(&b, true, 1100, 3000));  // only once
    TEST_ASSERT_EQUAL(BUTTON_NONE, hold(&b, false, 3000, 3100)); // no press on release
}

static void test_button_debounce(void) {
    Button b;
    button_init(&b, false, 0);
    // Contact bounce: 10 ms blips never count
    for (uint32_t t = 0; t < 200; t += 20) {
        TEST_ASSERT_EQUAL(BUTTON_NONE, button_update(&b, true, t));
        TEST_ASSERT_EQUAL(BUTTON_NONE, button_update(&b, false, t + 10));
    }
}

static void test_button_held_at_power_on(void) {
    Button b;
    button_init(&b, true, 0);  // the press that turned the board on
    TEST_ASSERT_EQUAL(BUTTON_NONE, hold(&b, true, 0, 3000));
    TEST_ASSERT_EQUAL(BUTTON_NONE, hold(&b, false, 3000, 3100));
    // Next press works normally
    TEST_ASSERT_EQUAL(BUTTON_LONG, hold(&b, true, 3100, 4300));
}

// ---- UI states ----------------------------------------------------------------

static GFXcanvas1 canvas(SCREEN_WIDTH, SCREEN_HEIGHT);

static void ride_at(RideStats *s, float kmh, double dist_m, float ele, uint32_t elapsed_s) {
    ride_stats_init(s);
    s->started    = true;
    s->start_time = 1000;
    s->last_time  = 1000 + elapsed_s;
    s->speed_mps  = kmh / 3.6f;
    s->distance_m = dist_m;
    s->moving_s   = elapsed_s;
    s->ele_m      = ele;
}

static void test_ui_full_cycle(void) {
    Ui ui;
    ui_init(&ui, 0);
    TEST_ASSERT_EQUAL(UI_IDLE, ui.state);

    // A short PWR press or BOOT does nothing while idle
    TEST_ASSERT_EQUAL(UI_ACT_NONE, ui_input(&ui, UI_IN_PWR, 0));
    TEST_ASSERT_EQUAL(UI_ACT_NONE, ui_input(&ui, UI_IN_PAGE, 0));
    TEST_ASSERT_EQUAL(UI_IDLE, ui.state);

    TEST_ASSERT_EQUAL(UI_ACT_START, ui_input(&ui, UI_IN_PWR_LONG, 1000));
    TEST_ASSERT_EQUAL(UI_WAKING, ui.state);
    ui_tick(&ui, 1000 + UI_WAKING_MS - 1);
    TEST_ASSERT_EQUAL(UI_WAKING, ui.state);
    ui_tick(&ui, 1000 + UI_WAKING_MS);
    TEST_ASSERT_EQUAL(UI_RIDING, ui.state);

    // BOOT cycles the pages: rider, dashboard, elevation, rider
    TEST_ASSERT_EQUAL(UI_PAGE_RIDER, ui.page);
    ui_input(&ui, UI_IN_PAGE, 5000);
    TEST_ASSERT_EQUAL(UI_PAGE_DASHBOARD, ui.page);
    ui_input(&ui, UI_IN_PAGE, 5000);
    TEST_ASSERT_EQUAL(UI_PAGE_ELEVATION, ui.page);
    ui_input(&ui, UI_IN_PAGE, 5000);
    TEST_ASSERT_EQUAL(UI_PAGE_RIDER, ui.page);

    // Pause, resume, pause, finish
    TEST_ASSERT_EQUAL(UI_ACT_PAUSE, ui_input(&ui, UI_IN_PWR, 6000));
    TEST_ASSERT_EQUAL(UI_ACT_RESUME, ui_input(&ui, UI_IN_PWR, 7000));
    TEST_ASSERT_EQUAL(UI_ACT_PAUSE, ui_input(&ui, UI_IN_PWR_LONG, 8000));  // long also pauses
    TEST_ASSERT_EQUAL(UI_PAUSED, ui.state);
    TEST_ASSERT_EQUAL(UI_ACT_FINISH, ui_input(&ui, UI_IN_PWR_LONG, 9000));
    TEST_ASSERT_EQUAL(UI_FINISHED, ui.state);
    TEST_ASSERT_EQUAL(UI_PAGE_RIDER, ui.page);

    // A short press on the summary doesn't restart anything
    TEST_ASSERT_EQUAL(UI_ACT_NONE, ui_input(&ui, UI_IN_PWR, 10000));
    TEST_ASSERT_EQUAL(UI_FINISHED, ui.state);
    ui_input(&ui, UI_IN_PWR_LONG, 11000);
    TEST_ASSERT_EQUAL(UI_IDLE, ui.state);
}

static void test_ui_finish_needs_pause_first(void) {
    Ui ui;
    ui_init(&ui, 0);
    ui_input(&ui, UI_IN_PWR_LONG, 0);
    ui_tick(&ui, UI_WAKING_MS);
    // Long press while riding pauses; it never finishes the ride directly
    TEST_ASSERT_EQUAL(UI_ACT_PAUSE, ui_input(&ui, UI_IN_PWR_LONG, 5000));
    TEST_ASSERT_EQUAL(UI_PAUSED, ui.state);
}

// ---- Values -------------------------------------------------------------------

static void test_values_imperial(void) {
    TEST_ASSERT_EQUAL(1, UI_IMPERIAL);
    RideStats s;
    ride_at(&s, 0, 0, 0, 3 * 3600 + 7 * 60 + 59);
    s.speed_mps  = 8.0f;        // 17.9 mph
    s.distance_m = 19956;       // 12.40 mi
    s.ele_gain_m = 139.2f;      // 456.7 ft
    char v[UI_VALUE_MAX];
    const char *unit;

    ui_format(UI_VAL_SPEED, &s, v, sizeof(v), &unit);
    TEST_ASSERT_EQUAL_STRING("18", v);
    TEST_ASSERT_EQUAL_STRING("mph", unit);

    ui_format(UI_VAL_DISTANCE, &s, v, sizeof(v), &unit);
    TEST_ASSERT_EQUAL_STRING("12.4", v);
    TEST_ASSERT_EQUAL_STRING("mi", unit);
    s.distance_m = 200000;      // 124.3 mi: no decimal past 100
    ui_format(UI_VAL_DISTANCE, &s, v, sizeof(v), &unit);
    TEST_ASSERT_EQUAL_STRING("124", v);

    ui_format(UI_VAL_TIME, &s, v, sizeof(v), &unit);
    TEST_ASSERT_EQUAL_STRING("3:07", v);
    TEST_ASSERT_EQUAL_STRING("h:mm", unit);

    ui_format(UI_VAL_CLIMB, &s, v, sizeof(v), &unit);
    TEST_ASSERT_EQUAL_STRING("457", v);
    TEST_ASSERT_EQUAL_STRING("ft up", unit);

    // 10 mi in 40 min of moving time: 15 mph
    ride_at(&s, 0, 16093.44, 0, 2400);
    ui_format(UI_VAL_AVG_SPEED, &s, v, sizeof(v), &unit);
    TEST_ASSERT_EQUAL_STRING("15", v);
    TEST_ASSERT_EQUAL_STRING("avg mph", unit);
}

// Which numbers each page shows, by state
static void test_page_contents(void) {
    Ui ui;
    ui_init(&ui, 0);
    RideStats s;
    ride_at(&s, 0, 16093.44, 0, 2400);
    s.speed_mps = 8.0f;

    ui_input(&ui, UI_IN_PWR_LONG, 0);
    ui_tick(&ui, UI_WAKING_MS);
    ui_render(&ui, &s, canvas);
    TEST_ASSERT_EQUAL_STRING("18", ui.shown.main);  // rider page: speed
    TEST_ASSERT_EQUAL(RIDER_CRUISING, ui.shown.pose);

    ui_input(&ui, UI_IN_PAGE, 0);
    ui_render(&ui, &s, canvas);
    TEST_ASSERT_EQUAL_STRING("18", ui.shown.main);  // dashboard: speed, distance, time
    TEST_ASSERT_EQUAL_STRING("10.0", ui.shown.left);
    TEST_ASSERT_EQUAL_STRING("0:40", ui.shown.right);
    TEST_ASSERT_EQUAL(RIDER_POSE_COUNT, ui.shown.pose);  // no rider

    ui_input(&ui, UI_IN_PAGE, 0);
    ui_render(&ui, &s, canvas);
    TEST_ASSERT_EQUAL_STRING("0", ui.shown.main);   // elevation
    TEST_ASSERT_EQUAL(RIDER_POSE_COUNT, ui.shown.pose);

    // Paused: rider page shows distance with the coffee
    ui_input(&ui, UI_IN_PAGE, 0);
    ui_input(&ui, UI_IN_PWR, 0);
    ui_render(&ui, &s, canvas);
    TEST_ASSERT_EQUAL_STRING("10.0", ui.shown.main);
    TEST_ASSERT_EQUAL(RIDER_COFFEE, ui.shown.pose);

    // Finished: couch and distance, then the dashboard with the average
    ui_input(&ui, UI_IN_PWR_LONG, 0);
    ui_render(&ui, &s, canvas);
    TEST_ASSERT_EQUAL_STRING("10.0", ui.shown.main);
    TEST_ASSERT_EQUAL(RIDER_COUCH, ui.shown.pose);
    ui_input(&ui, UI_IN_PAGE, 0);
    ui_render(&ui, &s, canvas);
    TEST_ASSERT_EQUAL_STRING("15", ui.shown.main);
}

// ---- Rider pose ---------------------------------------------------------------

static Ui riding_ui(void) {
    Ui ui;
    ui_init(&ui, 0);
    ui_input(&ui, UI_IN_PWR_LONG, 0);
    ui_tick(&ui, UI_WAKING_MS);
    return ui;
}

// Feed one fix per second at a steady speed and grade, for n seconds
static void ride_for(Ui *ui, RideStats *s, uint32_t *now, float kmh, float grade, int n) {
    for (int i = 0; i < n; i++) {
        *now += 1000;
        float step = kmh / 3.6f;
        s->speed_mps = step;
        s->distance_m += step;
        s->ele_m += step * grade;
        ui_ride_update(ui, s, *now);
    }
}

static void test_pose_changes_and_holds(void) {
    Ui ui = riding_ui();
    RideStats s;
    ride_at(&s, 20, 0, 100, 0);
    uint32_t now = UI_WAKING_MS;

    ride_for(&ui, &s, &now, 20, 0, 10);
    TEST_ASSERT_EQUAL(RIDER_CRUISING, ui.pose);

    // A 2 s burst of speed isn't enough to change pose
    ride_for(&ui, &s, &now, 35, 0, 2);
    ride_for(&ui, &s, &now, 20, 0, 5);
    TEST_ASSERT_EQUAL(RIDER_CRUISING, ui.pose);

    ride_for(&ui, &s, &now, 35, 0, 5);
    TEST_ASSERT_EQUAL(RIDER_FAST, ui.pose);

    // Climbing needs the grade over at least UI_GRADE_MIN_M of road
    ride_for(&ui, &s, &now, 10, 0.07f, 20);
    TEST_ASSERT_EQUAL(RIDER_CLIMBING, ui.pose);

    // Stopping takes UI_STOPPED_HOLD_MS before he dozes off
    ride_for(&ui, &s, &now, 0, 0, 4);
    TEST_ASSERT_EQUAL(RIDER_CLIMBING, ui.pose);
    ride_for(&ui, &s, &now, 0, 0, 3);
    TEST_ASSERT_EQUAL(RIDER_STOPPED, ui.pose);
}

// ---- Refreshes ----------------------------------------------------------------

static void test_refresh_policy(void) {
    Ui ui;
    ui_init(&ui, 0);
    RideStats s;
    ride_at(&s, 20, 1000, 100, 60);

    TEST_ASSERT_EQUAL(UI_REFRESH_FULL, ui_render(&ui, &s, canvas));  // first frame
    TEST_ASSERT_EQUAL(UI_REFRESH_NONE, ui_render(&ui, &s, canvas));  // nothing changed

    ui_input(&ui, UI_IN_PWR_LONG, 0);
    TEST_ASSERT_EQUAL(UI_REFRESH_FULL, ui_render(&ui, &s, canvas));  // waking up
    ui_tick(&ui, UI_WAKING_MS);
    s.speed_mps = 8.0f;  // 17.9 mph, clear of a rounding boundary
    TEST_ASSERT_EQUAL(UI_REFRESH_PARTIAL, ui_render(&ui, &s, canvas));  // first page

    // Same rounded speed: skip. New speed: partial.
    s.speed_mps += 0.05f;
    TEST_ASSERT_EQUAL(UI_REFRESH_NONE, ui_render(&ui, &s, canvas));
    s.speed_mps += 1.0f;
    TEST_ASSERT_EQUAL(UI_REFRESH_PARTIAL, ui_render(&ui, &s, canvas));

    // Speed changes on the elevation page don't refresh
    ui_input(&ui, UI_IN_PAGE, 5000);
    ui_input(&ui, UI_IN_PAGE, 5000);
    TEST_ASSERT_EQUAL(UI_REFRESH_PARTIAL, ui_render(&ui, &s, canvas));
    s.speed_mps += 3.0f;
    TEST_ASSERT_EQUAL(UI_REFRESH_NONE, ui_render(&ui, &s, canvas));

    // Neither do pose changes, since the rider isn't on this page
    ui.pose = RIDER_FAST;
    TEST_ASSERT_EQUAL(UI_REFRESH_NONE, ui_render(&ui, &s, canvas));

    // Stopping gets the full refresh on any page
    ui.pose = RIDER_STOPPED;
    TEST_ASSERT_EQUAL(UI_REFRESH_FULL, ui_render(&ui, &s, canvas));

    // Pausing too (coffee)
    ui.pose = RIDER_CRUISING;
    ui_render(&ui, &s, canvas);
    ui_input(&ui, UI_IN_PWR, 6000);
    TEST_ASSERT_EQUAL(UI_REFRESH_FULL, ui_render(&ui, &s, canvas));

    // Fallback after UI_FULL_EVERY partials, on the dashboard
    ui_input(&ui, UI_IN_PWR, 7000);   // resume, still on elevation
    ui_input(&ui, UI_IN_PAGE, 7000);  // rider
    ui_input(&ui, UI_IN_PAGE, 7000);  // dashboard
    TEST_ASSERT_EQUAL(UI_PAGE_DASHBOARD, ui.page);
    int fulls = 0;
    for (int i = 0; i < UI_FULL_EVERY + 1; i++) {
        s.distance_m += 200;  // changes the distance every time
        if (ui_render(&ui, &s, canvas) == UI_REFRESH_FULL) fulls++;
    }
    TEST_ASSERT_EQUAL(1, fulls);
}

// ---- Animation ----------------------------------------------------------------

static void test_riding_animation_steps_with_fixes(void) {
    Ui ui = riding_ui();
    RideStats s;
    ride_at(&s, 20, 0, 100, 0);
    uint32_t now = UI_WAKING_MS;
    ui_render(&ui, &s, canvas);

    // Cruising: a new frame every fix, and speed + frame go out together in
    // one refresh per fix, never two
    for (int i = 0; i < 6; i++) {
        uint8_t before = ui.frame;
        ride_for(&ui, &s, &now, i % 2 ? 20 : 22, 0, 1);
        TEST_ASSERT_NOT_EQUAL(before, ui.frame);
        TEST_ASSERT_EQUAL(UI_REFRESH_PARTIAL, ui_render(&ui, &s, canvas));
        TEST_ASSERT_EQUAL(UI_REFRESH_NONE, ui_render(&ui, &s, canvas));
    }

    // Climbing: every second fix (a slow grind)
    ride_for(&ui, &s, &now, 10, 0.07f, 20);
    TEST_ASSERT_EQUAL(RIDER_CLIMBING, ui.pose);
    uint8_t f0 = ui.frame;
    int changes = 0;
    for (int i = 0; i < 8; i++) {
        ride_for(&ui, &s, &now, 10, 0.07f, 1);
        if (ui.frame != f0) changes++;
        f0 = ui.frame;
    }
    TEST_ASSERT_EQUAL(4, changes);
}

static void test_no_animation_off_the_rider_page(void) {
    Ui ui = riding_ui();
    RideStats s;
    ride_at(&s, 20, 0, 100, 0);
    uint32_t now = UI_WAKING_MS;
    ui_input(&ui, UI_IN_PAGE, now);  // dashboard
    ui_render(&ui, &s, canvas);
    s.speed_mps = 8.0f;
    ui_render(&ui, &s, canvas);
    // Same speed, frames keep stepping: nothing to refresh
    for (int i = 0; i < 5; i++) {
        now += 1000;
        ui_ride_update(&ui, &s, now);
        TEST_ASSERT_EQUAL(UI_REFRESH_NONE, ui_render(&ui, &s, canvas));
    }
}

static void test_rest_animation_and_timeout(void) {
    Ui ui;
    ui_init(&ui, 0);
    RideStats s;
    ride_stats_init(&s);
    TEST_ASSERT_EQUAL(UI_REFRESH_FULL, ui_render(&ui, &s, canvas));

    // Asleep in bed: a new frame every 2 s
    ui_tick(&ui, 1999);
    TEST_ASSERT_EQUAL(UI_REFRESH_NONE, ui_render(&ui, &s, canvas));
    ui_tick(&ui, 2000);
    TEST_ASSERT_EQUAL(UI_REFRESH_PARTIAL, ui_render(&ui, &s, canvas));
    ui_tick(&ui, 4000);
    TEST_ASSERT_EQUAL(UI_REFRESH_PARTIAL, ui_render(&ui, &s, canvas));

    // After the timeout he holds still on frame 0 and the screen stops
    // refreshing
    int refreshes = 0;
    for (uint32_t t = 4000; t <= UI_ANIM_TIMEOUT_MS + 60000; t += 500) {
        ui_tick(&ui, t);
        if (ui_render(&ui, &s, canvas) != UI_REFRESH_NONE && t > UI_ANIM_TIMEOUT_MS) refreshes++;
    }
    TEST_ASSERT_EQUAL(0, ui.frame);
    TEST_ASSERT_TRUE(refreshes <= 1);  // at most the step back to frame 0
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_button_short_press);
    RUN_TEST(test_button_long_press_fires_while_held);
    RUN_TEST(test_button_debounce);
    RUN_TEST(test_button_held_at_power_on);
    RUN_TEST(test_ui_full_cycle);
    RUN_TEST(test_ui_finish_needs_pause_first);
    RUN_TEST(test_values_imperial);
    RUN_TEST(test_page_contents);
    RUN_TEST(test_pose_changes_and_holds);
    RUN_TEST(test_refresh_policy);
    RUN_TEST(test_riding_animation_steps_with_fixes);
    RUN_TEST(test_no_animation_off_the_rider_page);
    RUN_TEST(test_rest_animation_and_timeout);
    return UNITY_END();
}
