#pragma once

#include <stddef.h>
#include <stdint.h>

#include <Adafruit_GFX.h>

#include "ride.h"
#include "screen.h"

// What the device shows and how the buttons drive it. Independent of the
// hardware: inputs are button actions and ride stats, output is a drawn
// canvas plus which kind of refresh it needs. The caller owns recording
// (stats, GPX) and acts on the UiAction each input returns.
//
// States and buttons (BOOT = page, PWR = ride control):
//
//   IDLE      asleep in bed           PWR long: start  -> WAKING
//   WAKING    sits up, a few seconds  (then)           -> RIDING
//   RIDING    page + live rider       PWR: pause       -> PAUSED
//   PAUSED    page + coffee           PWR: resume      -> RIDING
//                                     PWR long: finish -> FINISHED
//   FINISHED  summary + couch         PWR long: done   -> IDLE
//
// BOOT moves to the next page in RIDING, PAUSED and FINISHED.
//
// Pages:                Riding               Paused / Finished
//   RIDER      one number + rider: speed     distance (speed would be 0)
//   DASHBOARD  speed, distance and time      finished: average speed
//   ELEVATION  climbing so far

enum UiState { UI_IDLE, UI_WAKING, UI_RIDING, UI_PAUSED, UI_FINISHED };

enum UiPage { UI_PAGE_RIDER, UI_PAGE_DASHBOARD, UI_PAGE_ELEVATION, UI_PAGE_COUNT };

// Units on screen. Recording and GPX are always metric.
#define UI_IMPERIAL 1

enum UiInput { UI_IN_PAGE, UI_IN_PWR, UI_IN_PWR_LONG };

// What the caller should do with the recording
enum UiAction { UI_ACT_NONE, UI_ACT_START, UI_ACT_PAUSE, UI_ACT_RESUME, UI_ACT_FINISH };

enum UiRefresh { UI_REFRESH_NONE, UI_REFRESH_PARTIAL, UI_REFRESH_FULL };

#define UI_WAKING_MS        3000

// Fallback full refresh against ghosting. Most full refreshes happen at
// stops and state changes instead. 30-40 looked fine on the V2 board.
#define UI_FULL_EVERY       40

// Rider pose while riding. A new pose has to hold for a while before the
// rider changes, so he doesn't flicker at a threshold (and waste refreshes).
#define UI_FAST_MPS         (30.0f / 3.6f)
#define UI_CLIMB_GRADE      0.04f
#define UI_POSE_HOLD_MS     3000
#define UI_STOPPED_HOLD_MS  5000

// Grade over the last stretch of road: at least UI_GRADE_MIN_M long,
// looking back at most UI_GRADE_WINDOW fixes (seconds).
#define UI_GRADE_WINDOW     30
#define UI_GRADE_MIN_M      30.0f

#define UI_VALUE_MAX        8

// Everything that decides what is on screen. Equal frames need no refresh.
struct UiFrame {
    UiState   state;
    UiPage    page;
    RiderPose pose;     // RIDER_POSE_COUNT when the page has no rider
    bool      stopped;  // riding but stopped, on any page (for the full refresh)
    char      main[UI_VALUE_MAX];
    char      left[UI_VALUE_MAX];
    char      right[UI_VALUE_MAX];
};

struct Ui {
    UiState   state;
    UiPage    page;
    uint32_t  state_since_ms;

    RiderPose pose;              // riding pose currently shown
    RiderPose candidate;         // pose the ride suggests
    uint32_t  candidate_since_ms;

    // Recent (distance, elevation) per fix, for the grade
    float     hist_dist[UI_GRADE_WINDOW];
    float     hist_ele[UI_GRADE_WINDOW];
    uint8_t   hist_len, hist_head;

    UiFrame   shown;
    bool      has_shown;
    uint16_t  partials;          // since the last full refresh
};

void ui_init(Ui *ui, uint32_t now_ms);

// A button action. Returns what to do with the recording.
UiAction ui_input(Ui *ui, UiInput in, uint32_t now_ms);

// Once per new fix while riding, after updating the stats
void ui_ride_update(Ui *ui, const RideStats *stats, uint32_t now_ms);

// Timers (waking up). Call often.
void ui_tick(Ui *ui, uint32_t now_ms);

// Draw the current frame into the canvas if it changed since the last call
// and say how to refresh. UI_REFRESH_NONE leaves the canvas untouched.
UiRefresh ui_render(Ui *ui, const RideStats *stats, GFXcanvas1 &canvas);

// One value as shown on screen, with its unit, in UI_IMPERIAL or metric
enum UiValue { UI_VAL_SPEED, UI_VAL_AVG_SPEED, UI_VAL_DISTANCE, UI_VAL_TIME, UI_VAL_CLIMB };
void ui_format(UiValue v, const RideStats *stats, char *buf, size_t size, const char **unit);
