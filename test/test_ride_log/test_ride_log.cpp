// Unit tests for lib/ride_log: GPX files on disk and repair after a power cut.
// Run: pio test -e native. Uses a fresh temp directory per test.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <unity.h>

#include "ride_log.h"

static char dir[128];

void setUp() {
    const char *tmp = getenv("TMPDIR");
    snprintf(dir, sizeof(dir), "%s/ride_log_XXXXXX", tmp ? tmp : "/tmp");
    TEST_ASSERT_NOT_NULL(mkdtemp(dir));
}

void tearDown() {
    DIR *d = opendir(dir);
    char path[256];
    for (struct dirent *e; d && (e = readdir(d));) {
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        remove(path);
    }
    if (d) closedir(d);
    remove(dir);
}

#define T0 1793690142u  // 2026-11-03T07:15:42Z

static GpsFix fix_at(uint32_t t, bool valid = true) {
    GpsFix f = {};
    f.time  = t;
    f.lat   = 51.5 + (t - T0) * 1e-5;
    f.lon   = -0.125;
    f.ele_m = 35.0f;
    f.valid = valid;
    return f;
}

static bool point_at(RideLog *log, uint32_t t) {
    GpsFix f = fix_at(t);
    return ride_log_point(log, &f);
}

static int count_files(const char *suffix) {
    int n = 0;
    DIR *d = opendir(dir);
    for (struct dirent *e; d && (e = readdir(d));) {
        size_t len = strlen(e->d_name), s = strlen(suffix);
        if (len >= s && strcmp(e->d_name + len - s, suffix) == 0) n++;
    }
    if (d) closedir(d);
    return n;
}

// Whole file into buf (NUL-terminated)
static size_t slurp(const char *name, char *buf, size_t size) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
    return n;
}

static int count(const char *haystack, const char *needle) {
    int n = 0;
    for (const char *p = haystack; (p = strstr(p, needle)); p++) n++;
    return n;
}

static bool ends_with_footer(const char *s) {
    const char *footer = "</trkseg></trk>\n</gpx>\n";
    size_t n = strlen(s), f = strlen(footer);
    return n >= f && strcmp(s + n - f, footer) == 0;
}

static char content[16384];

static void test_ride_with_pause(void) {
    RideLog log;
    ride_log_start(&log, dir);
    for (uint32_t t = 0; t < 5; t++) TEST_ASSERT_TRUE(point_at(&log, T0 + t));
    TEST_ASSERT_EQUAL(1, count_files(".part"));  // written as it goes
    TEST_ASSERT_TRUE(ride_log_pause(&log));
    for (uint32_t t = 60; t < 63; t++) TEST_ASSERT_TRUE(point_at(&log, T0 + t));
    TEST_ASSERT_TRUE(ride_log_finish(&log));

    TEST_ASSERT_EQUAL(0, count_files(".part"));
    TEST_ASSERT_TRUE(slurp("20261103-071542.gpx", content, sizeof(content)) > 0);
    TEST_ASSERT_EQUAL(8, count(content, "<trkpt "));
    TEST_ASSERT_EQUAL(1, count(content, "</trkseg><trkseg>\n"));  // the pause
    TEST_ASSERT_TRUE(ends_with_footer(content));
}

static void test_no_fix_no_file(void) {
    RideLog log;
    ride_log_start(&log, dir);
    GpsFix searching = fix_at(T0, false);
    TEST_ASSERT_TRUE(ride_log_point(&log, &searching));
    TEST_ASSERT_TRUE(ride_log_pause(&log));
    TEST_ASSERT_TRUE(ride_log_finish(&log));
    TEST_ASSERT_EQUAL(0, count_files(".part") + count_files(".gpx"));
}

static void test_named_after_first_fix(void) {
    RideLog log;
    ride_log_start(&log, dir);
    GpsFix searching = fix_at(T0, false);
    ride_log_point(&log, &searching);
    GpsFix first = fix_at(T0 + 75);  // 07:16:57
    ride_log_point(&log, &first);
    ride_log_finish(&log);
    TEST_ASSERT_TRUE(slurp("20261103-071657.gpx", content, sizeof(content)) > 0);
    TEST_ASSERT_EQUAL(1, count(content, "<trkpt "));
}

// A ride cut off by a power cut: file left open, last line half written
static void cut_off_ride(const char *partial_tail) {
    RideLog log;
    ride_log_start(&log, dir);
    for (uint32_t t = 0; t < 12; t++) point_at(&log, T0 + t);
    fputs(partial_tail, log.f);
    fclose(log.f);  // what reached the flash; no footer, still .part
}

static void test_recover_cut_mid_line(void) {
    cut_off_ride("<trkpt lat=\"51.50");
    TEST_ASSERT_EQUAL(1, ride_log_recover(dir));
    TEST_ASSERT_EQUAL(0, count_files(".part"));
    slurp("20261103-071542.gpx", content, sizeof(content));
    TEST_ASSERT_EQUAL(12, count(content, "</trkpt>"));
    TEST_ASSERT_EQUAL(0, count(content, "lat=\"51.50\""));  // the half line is gone
    TEST_ASSERT_TRUE(ends_with_footer(content));
    TEST_ASSERT_EQUAL(0, ride_log_recover(dir));  // nothing left to do
}

static void test_recover_cut_in_header(void) {
    char path[256];
    snprintf(path, sizeof(path), "%s/20261103-071542.part", dir);
    FILE *f = fopen(path, "w");
    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gpx ver", f);
    fclose(f);
    TEST_ASSERT_EQUAL(0, ride_log_recover(dir));  // no points: nothing to keep
    TEST_ASSERT_EQUAL(0, count_files(".part") + count_files(".gpx"));
}

static void test_recover_cut_before_rename(void) {
    cut_off_ride("</trkseg></trk>\n</gpx>\n");  // finished, not yet renamed
    TEST_ASSERT_EQUAL(1, ride_log_recover(dir));
    slurp("20261103-071542.gpx", content, sizeof(content));
    TEST_ASSERT_EQUAL(1, count(content, "</gpx>"));  // no second footer
    TEST_ASSERT_TRUE(ends_with_footer(content));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_ride_with_pause);
    RUN_TEST(test_no_fix_no_file);
    RUN_TEST(test_named_after_first_fix);
    RUN_TEST(test_recover_cut_mid_line);
    RUN_TEST(test_recover_cut_in_header);
    RUN_TEST(test_recover_cut_before_rename);
    return UNITY_END();
}
