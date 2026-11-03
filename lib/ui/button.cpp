#include "button.h"

void button_init(Button *b, bool held_now, uint32_t now_ms) {
    b->raw        = held_now;
    b->raw_since  = now_ms;
    b->down       = held_now;
    b->down_since = now_ms;
    b->long_fired = false;
    b->armed      = !held_now;
}

ButtonEvent button_update(Button *b, bool pressed, uint32_t now_ms) {
    if (pressed != b->raw) {
        b->raw       = pressed;
        b->raw_since = now_ms;
    }

    // Debounced edge: the level has been steady long enough
    if (b->raw != b->down && now_ms - b->raw_since >= BUTTON_DEBOUNCE_MS) {
        b->down = b->raw;
        if (b->down) {
            b->down_since = now_ms;
            b->long_fired = false;
        } else {
            bool was_armed = b->armed;
            bool was_long  = b->long_fired;
            b->armed = true;
            if (was_armed && !was_long) return BUTTON_PRESS;
        }
        return BUTTON_NONE;
    }

    if (b->down && b->armed && !b->long_fired && now_ms - b->down_since >= BUTTON_LONG_MS) {
        b->long_fired = true;
        return BUTTON_LONG;
    }
    return BUTTON_NONE;
}
