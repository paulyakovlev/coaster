#pragma once

#include <stddef.h>
#include <stdint.h>

// Ride start sound: a short tone, generated on the fly (no sound files).
// Kept to one sound on purpose: beeping on every button is distracting on a
// group ride.
// The synth part is hardware-independent; sound_es8311.cpp plays it through
// the board's ES8311 codec and speaker amplifier.

#define SOUND_RATE 16000  // samples per second

// Small speakers barely reproduce anything under ~800 Hz, so the start sound
// sits around 1.3-1.8 kHz: one note rising from E6 to A6.
#define SOUND_START_HZ    1319
#define SOUND_START_TO_HZ 1760
#define SOUND_START_MS    180

// Plays one tone that glides from hz to to_hz into a sample buffer, a chunk at a time
struct SoundPlayer {
    float    hz, to_hz;
    uint32_t len;        // samples
    uint32_t pos;        // current sample
    float    phase;      // 0..1
    float    amplitude;  // 0..1 of full scale
};

void sound_init(SoundPlayer *p, uint16_t hz, uint16_t to_hz, uint16_t ms, float amplitude);

// Fill up to n mono samples. Returns how many were written; 0 when done.
size_t sound_fill(SoundPlayer *p, int16_t *out, size_t n);

// Hardware (ES8311 on the Waveshare board). Not built for the simulator.
// sound_begin powers the codec and needs Wire started on the shared bus.
bool    sound_begin();
void    sound_end();               // codec and amplifier off; safe to call any time
void    sound_play_start();        // blocks until the sound has played
void    sound_tone(uint16_t hz, uint16_t ms);
void    sound_set_volume(uint8_t reg);  // codec DAC volume: 0xBF = 0 dB, 0.5 dB steps
uint8_t sound_volume();
