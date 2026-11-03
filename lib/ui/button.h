#pragma once

#include <stdint.h>

// Debounced button with short and long press, independent of the hardware:
// the caller reads the pin and passes the time.
//
// A long press fires while the button is still held, as soon as it reaches
// BUTTON_LONG_MS, so the device reacts without waiting for the release. A
// short press fires on release. Never both for one press.

#define BUTTON_DEBOUNCE_MS 30
#define BUTTON_LONG_MS     1000

enum ButtonEvent { BUTTON_NONE, BUTTON_PRESS, BUTTON_LONG };

struct Button {
    bool     raw;         // last level read
    uint32_t raw_since;   // when raw last changed
    bool     down;        // debounced
    uint32_t down_since;
    bool     long_fired;  // this press already reported as long
    bool     armed;       // false: ignore until released once
};

// held_now: the button's level at startup. If it's already held (the press
// that powered the board on), it's ignored until released.
void button_init(Button *b, bool held_now, uint32_t now_ms);

// Call every few milliseconds with the current level (true = pressed)
ButtonEvent button_update(Button *b, bool pressed, uint32_t now_ms);
