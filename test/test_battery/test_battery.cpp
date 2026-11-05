// Unit tests for lib/battery: LiPo voltage to percent.
// Run: pio test -e native

#include <unity.h>

#include "battery.h"

void setUp() {}
void tearDown() {}

static void test_ends_clamp(void) {
    TEST_ASSERT_EQUAL(100, battery_percent(4350));  // charging
    TEST_ASSERT_EQUAL(100, battery_percent(4200));
    TEST_ASSERT_EQUAL(0, battery_percent(3300));
    TEST_ASSERT_EQUAL(0, battery_percent(2900));
    TEST_ASSERT_EQUAL(0, battery_percent(0));  // no battery / bad read
}

static void test_curve_points_and_between(void) {
    TEST_ASSERT_EQUAL(63, battery_percent(3900));
    TEST_ASSERT_EQUAL(42, battery_percent(3800));
    TEST_ASSERT_EQUAL(52, battery_percent(3850));  // halfway: 42 + 21 / 2
    TEST_ASSERT_EQUAL(3, battery_percent(3450));   // halfway: 0 + 6 / 2
}

static void test_never_rises_as_voltage_falls(void) {
    uint8_t last = 100;
    for (uint16_t mv = 4300; mv > 3000; mv--) {
        uint8_t p = battery_percent(mv);
        TEST_ASSERT_TRUE(p <= last);
        last = p;
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_ends_clamp);
    RUN_TEST(test_curve_points_and_between);
    RUN_TEST(test_never_rises_as_voltage_falls);
    return UNITY_END();
}
