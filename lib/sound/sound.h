#pragma once

#include <stddef.h>
#include <stdint.h>

// Ride start sound: a short tone, generated on the fly (no sound files).
// Kept to one sound on purpose: beeping on every button is distracting on a
// group ride.
// The synth part is hardware-independent; sound_es8311.cpp plays it through
// the board's ES8311 codec and speaker amplifier.

enum SoundId { SOUND_START, SOUND_COUNT };

#define SOUND_RATE 16000  // samples per second

// A note that glides from hz to to_hz (equal for a steady note)
struct SoundNote {
    uint16_t hz;  // 0 = silence
    uint16_t to_hz;
    uint16_t ms;
};

// Plays one sound into a sample buffer, a chunk at a time
struct SoundPlayer {
    const SoundNote *notes;
    uint8_t          count;
    uint8_t          index;     // current note
    uint32_t         pos;       // sample within the current note
    float            phase;     // 0..1
    float            amplitude; // 0..1 of full scale
};

void sound_start(SoundPlayer *p, SoundId id, float amplitude);

// Fill up to n mono samples. Returns how many were written; 0 when done.
size_t sound_fill(SoundPlayer *p, int16_t *out, size_t n);

// Total length in ms
uint32_t sound_length_ms(SoundId id);

// Hardware (ES8311 on the Waveshare board). Not built for the simulator.
// sound_begin powers the codec and needs Wire started on the shared bus.
bool    sound_begin();
void    sound_end();               // codec and amplifier off; safe to call any time
void    sound_play(SoundId id);    // blocks until the sound has played
void    sound_tone(uint16_t hz, uint16_t ms);
void    sound_set_volume(uint8_t reg);  // codec DAC volume: 0xBF = 0 dB, 0.5 dB steps
uint8_t sound_volume();
