// Unit tests for the sound synth in lib/sound: pio test -e native

#include <stdlib.h>
#include <unity.h>

#include "sound.h"

void setUp() {}
void tearDown() {}

// Every sample of the start sound, in chunks like the player writes them
static size_t render(int16_t *out, size_t max) {
    SoundPlayer p;
    sound_init(&p, SOUND_START_HZ, SOUND_START_TO_HZ, SOUND_START_MS, 0.5f);
    size_t total = 0, n;
    while ((n = sound_fill(&p, out + total, max - total < 100 ? max - total : 100)) > 0) {
        total += n;
    }
    return total;
}

static int16_t buf[SOUND_RATE];  // up to 1 s

static void test_length(void) {
    TEST_ASSERT_EQUAL_UINT32(SOUND_START_MS * SOUND_RATE / 1000, render(buf, SOUND_RATE));
}

// Fades keep the sound from clicking: it starts and ends at silence
static void test_starts_and_ends_quiet(void) {
    size_t n = render(buf, SOUND_RATE);
    TEST_ASSERT_INT16_WITHIN(500, 0, buf[0]);
    TEST_ASSERT_INT16_WITHIN(500, 0, buf[n - 1]);
}

static void test_amplitude(void) {
    size_t n    = render(buf, SOUND_RATE);
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
    size_t n = render(buf, SOUND_RATE);
    TEST_ASSERT_GREATER_THAN(crossings(buf, n / 2), crossings(buf + n / 2, n - n / 2));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_length);
    RUN_TEST(test_starts_and_ends_quiet);
    RUN_TEST(test_amplitude);
    RUN_TEST(test_start_rises);
    return UNITY_END();
}
