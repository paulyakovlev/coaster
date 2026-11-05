#include "sound.h"

#include <math.h>

// Fade in and out at the edges so the tone doesn't click
#define FADE_MS 4

void sound_init(SoundPlayer *p, uint16_t hz, uint16_t to_hz, uint16_t ms, float amplitude) {
    p->hz        = hz;
    p->to_hz     = to_hz;
    p->len       = (uint32_t)ms * SOUND_RATE / 1000;
    p->pos       = 0;
    p->phase     = 0;
    p->amplitude = amplitude;
}

size_t sound_fill(SoundPlayer *p, int16_t *out, size_t n) {
    uint32_t fade = FADE_MS * SOUND_RATE / 1000;
    if (fade > p->len / 2) fade = p->len / 2;

    size_t written = 0;
    while (written < n && p->pos < p->len) {
        float env = 1;
        if (p->pos < fade) env = (float)p->pos / fade;
        else if (p->len - p->pos <= fade) env = (float)(p->len - p->pos) / fade;
        float s = sinf(2 * (float)M_PI * p->phase) * env * p->amplitude;
        p->phase += (p->hz + (p->to_hz - p->hz) * p->pos / p->len) / SOUND_RATE;
        if (p->phase >= 1) p->phase -= 1;
        out[written++] = (int16_t)(s * 32767);
        p->pos++;
    }
    return written;
}
