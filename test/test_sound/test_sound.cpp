// Unit tests for the sound synth in lib/sound: pio test -e native

#include <stdlib.h>
#include <unity.h>

#include "sound.h"

void setUp() {}
void tearDown() {}

// Every sample of a sound, in chunks like the player writes them
static size_t render(SoundId id, int16_t *out, size_t max) {
    SoundPlayer p;
    sound_start(&p, id, 0.5f);
    size_t total = 0, n;
    while ((n = sound_fill(&p, out + total, max - total < 100 ? max - total : 100)) > 0) {
        total += n;
    }
    return total;
}

static int16_t buf[SOUND_RATE];  // up to 1 s

static void test_length_matches_notes(void) {
    for (int id = 0; id < SOUND_COUNT; id++) {
        size_t n = render((SoundId)id, buf, SOUND_RATE);
        TEST_ASSERT_EQUAL_UINT32(sound_length_ms((SoundId)id) * SOUND_RATE / 1000, n);
    }
}

// Fades keep each sound from clicking: it starts and ends at silence
static void test_starts_and_ends_quiet(void) {
    for (int id = 0; id < SOUND_COUNT; id++) {
        size_t n = render((SoundId)id, buf, SOUND_RATE);
        TEST_ASSERT_INT16_WITHIN(500, 0, buf[0]);
        TEST_ASSERT_INT16_WITHIN(500, 0, buf[n - 1]);
    }
}

static void test_amplitude(void) {
    size_t n    = render(SOUND_START, buf, SOUND_RATE);
    int    peak = 0;
    for (size_t i = 0; i < n; i++) peak = abs(buf[i]) > peak ? abs(buf[i]) : peak;
    TEST_ASSERT_INT_WITHIN(400, 16383, peak);
}

// Rising glide: more zero crossings in the second half than the first
static int crossings(const int16_t *s, size_t n) {
    int c = 0;
    for (size_t i = 1; i < n; i++) c += (s[i - 1] < 0) != (s[i] < 0);
    return c;
}

static void test_start_rises(void) {
    size_t n = render(SOUND_START, buf, SOUND_RATE);
    TEST_ASSERT_GREATER_THAN(crossings(buf, n / 2), crossings(buf + n / 2, n - n / 2));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_length_matches_notes);
    RUN_TEST(test_starts_and_ends_quiet);
    RUN_TEST(test_amplitude);
    RUN_TEST(test_start_rises);
    return UNITY_END();
}
