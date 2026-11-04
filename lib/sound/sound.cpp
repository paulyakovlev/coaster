#include "sound.h"

#include <math.h>

// Small speakers barely reproduce anything under ~800 Hz, so the note sits
// around 1.3-1.8 kHz: one note rising from E6 (1319) to A6 (1760).
static const SoundNote START[] = {{1319, 1760, 180}};

struct SoundDef {
    const SoundNote *notes;
    uint8_t          count;
};

#define DEF(n) {n, sizeof(n) / sizeof(n[0])}
static const SoundDef SOUNDS[SOUND_COUNT] = {DEF(START)};

// Fade in and out at each note's edges so notes don't click
#define FADE_MS 4

void sound_start(SoundPlayer *p, SoundId id, float amplitude) {
    p->notes     = SOUNDS[id].notes;
    p->count     = SOUNDS[id].count;
    p->index     = 0;
    p->pos       = 0;
    p->phase     = 0;
    p->amplitude = amplitude;
}

size_t sound_fill(SoundPlayer *p, int16_t *out, size_t n) {
    size_t written = 0;
    while (written < n && p->index < p->count) {
        const SoundNote &note = p->notes[p->index];
        uint32_t len  = (uint32_t)note.ms * SOUND_RATE / 1000;
        uint32_t fade = FADE_MS * SOUND_RATE / 1000;
        if (fade > len / 2) fade = len / 2;

        while (written < n && p->pos < len) {
            float s = 0;
            if (note.hz) {
                float env = 1;
                if (p->pos < fade) env = (float)p->pos / fade;
                else if (len - p->pos <= fade) env = (float)(len - p->pos) / fade;
                s = sinf(2 * (float)M_PI * p->phase) * env * p->amplitude;
                float hz = note.hz + ((float)note.to_hz - note.hz) * p->pos / len;
                p->phase += hz / SOUND_RATE;
                if (p->phase >= 1) p->phase -= 1;
            }
            out[written++] = (int16_t)(s * 32767);
            p->pos++;
        }
        if (p->pos >= len) {
            p->index++;
            p->pos   = 0;
            p->phase = 0;
        }
    }
    return written;
}

uint32_t sound_length_ms(SoundId id) {
    uint32_t ms = 0;
    for (uint8_t i = 0; i < SOUNDS[id].count; i++) ms += SOUNDS[id].notes[i].ms;
    return ms;
}
