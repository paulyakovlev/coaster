// Unit tests for lib/ride, run on this computer: pio test -e native

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "ride.h"

void setUp() {}
void tearDown() {}

// ---- Helpers ----------------------------------------------------------------

// Feed a whole string; returns how many fixes completed, last one in *out
static int feed(NmeaParser *p, const char *s, GpsFix *out) {
    int fixes = 0;
    for (; *s; s++) fixes += nmea_feed(p, *s, out);
    return fixes;
}

// Wrap a sentence body in "$...*hh\r\n"
static void sentence(char *buf, size_t size, const char *body) {
    uint8_t sum = 0;
    for (const char *c = body; *c; c++) sum ^= (uint8_t)*c;
    snprintf(buf, size, "$%s*%02X\r\n", body, sum);
}

// One fix per second heading north from (lat0, lon0)
static GpsFix fix_at(uint32_t t, double lat, double lon, float speed, float ele) {
    GpsFix f = {};
    f.time = t;
    f.lat = lat;
    f.lon = lon;
    f.speed_mps = speed;
    f.ele_m = ele;
    f.valid = true;
    return f;
}

#define T0 1793690142u  // 2026-11-03T07:15:42Z
#define METERS_PER_DEG_LAT 111194.93  // on a 6371 km sphere

// Small deterministic noise in [-amp, amp]
static float noise(uint32_t i, float amp) {
    uint32_t x = i * 2654435761u;
    x ^= x >> 13;
    return amp * (2.0f * (x % 1000) / 999.0f - 1.0f);
}

// ---- Time -------------------------------------------------------------------

static void test_time(void) {
    TEST_ASSERT_EQUAL_UINT32(0, utc_to_unix(1970, 1, 1, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(951782400u, utc_to_unix(2000, 2, 29, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(T0, utc_to_unix(2026, 11, 3, 7, 15, 42));

    char buf[21];
    unix_to_iso8601(T0, buf);
    TEST_ASSERT_EQUAL_STRING("2026-11-03T07:15:42Z", buf);
    unix_to_iso8601(951782400u + 86399, buf);
    TEST_ASSERT_EQUAL_STRING("2000-02-29T23:59:59Z", buf);
}

// ---- NMEA -------------------------------------------------------------------

// Textbook example sentences, with their real checksums
#define RMC "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n"
#define GGA "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n"

static void test_nmea_fix(void) {
    NmeaParser p;
    nmea_init(&p);
    GpsFix f = {};

    TEST_ASSERT_EQUAL(0, feed(&p, RMC, &f));  // half a fix
    TEST_ASSERT_EQUAL(1, feed(&p, GGA, &f));

    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_EQUAL_UINT32(764426119u, f.time);  // 1994-03-23 12:35:19
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 48.1173, f.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 11.0 + 31.0 / 60, f.lon);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.4f * 0.514444f, f.speed_mps);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 545.4f, f.ele_m);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.9f, f.hdop);
    TEST_ASSERT_EQUAL_UINT8(8, f.sats);
}

static void test_nmea_order_and_noise(void) {
    NmeaParser p;
    nmea_init(&p);
    GpsFix f = {};

    // GGA first, other sentences and line noise in between
    const char *stream = "garbage" GGA "$GPVTG,084.4,T,,M,022.4,N,041.5,K*58\r\n"
                         "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39\r\n" RMC;
    TEST_ASSERT_EQUAL(1, feed(&p, stream, &f));
    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_EQUAL_UINT32(764426119u, f.time);
}

static void test_nmea_bad_checksum(void) {
    NmeaParser p;
    nmea_init(&p);
    GpsFix f = {};
    const char *bad_gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48\r\n";
    TEST_ASSERT_EQUAL(0, feed(&p, RMC, &f));
    TEST_ASSERT_EQUAL(0, feed(&p, bad_gga, &f));
}

static void test_nmea_mismatched_seconds(void) {
    NmeaParser p;
    nmea_init(&p);
    GpsFix f = {};
    char gga[100];
    sentence(gga, sizeof(gga), "GPGGA,123520,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
    TEST_ASSERT_EQUAL(0, feed(&p, RMC, &f));
    TEST_ASSERT_EQUAL(0, feed(&p, gga, &f));  // different second
}

static void test_nmea_no_fix(void) {
    NmeaParser p;
    nmea_init(&p);
    GpsFix f = {};
    char rmc[100], gga[100];
    // What a NEO-6M sends while searching: time and date known, no position
    sentence(rmc, sizeof(rmc), "GPRMC,071542.00,V,,,,,,,031126,,,N");
    sentence(gga, sizeof(gga), "GPGGA,071542.00,,,,,0,00,99.99,,,,,,");
    TEST_ASSERT_EQUAL(0, feed(&p, rmc, &f));
    TEST_ASSERT_EQUAL(1, feed(&p, gga, &f));
    TEST_ASSERT_FALSE(f.valid);
    TEST_ASSERT_EQUAL_UINT32(T0, f.time);
}

static void test_nmea_gn_talker(void) {
    NmeaParser p;
    nmea_init(&p);
    GpsFix f = {};
    char rmc[100], gga[100];
    sentence(rmc, sizeof(rmc), "GNRMC,071542.00,A,5130.000,N,00007.500,W,10.0,0.0,031126,,,A");
    sentence(gga, sizeof(gga), "GNGGA,071542.00,5130.000,N,00007.500,W,1,09,1.1,35.0,M,45.0,M,,");
    feed(&p, rmc, &f);
    TEST_ASSERT_EQUAL(1, feed(&p, gga, &f));
    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 51.5, f.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -0.125, f.lon);
}

// ---- Stats ------------------------------------------------------------------

static void test_distance(void) {
    // One degree of latitude
    TEST_ASSERT_DOUBLE_WITHIN(1, METERS_PER_DEG_LAT, ride_distance_m(0, 0, 1, 0));
    // London to Paris, about 343.5 km
    TEST_ASSERT_DOUBLE_WITHIN(500, 343500, ride_distance_m(51.5074, -0.1278, 48.8566, 2.3522));
}

static void test_stats_steady_ride(void) {
    RideStats s;
    ride_stats_init(&s);

    // 10 minutes north at 5 m/s (18 km/h), with position noise of about 1 m
    const float speed = 5;
    for (uint32_t i = 0; i <= 600; i++) {
        double lat = 45 + (i * speed + noise(i, 1)) / METERS_PER_DEG_LAT;
        GpsFix f = fix_at(T0 + i, lat, 7, speed, 100);
        ride_stats_update(&s, &f);
    }

    TEST_ASSERT_EQUAL_UINT32(600, ride_stats_elapsed_s(&s));
    TEST_ASSERT_EQUAL_UINT32(600, s.moving_s);
    TEST_ASSERT_DOUBLE_WITHIN(30, 3000, s.distance_m);  // noise adds a little
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 5, ride_stats_avg_speed_mps(&s));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5, s.max_speed_mps);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0, s.ele_gain_m);
}

static void test_stats_stop_adds_no_distance(void) {
    RideStats s;
    ride_stats_init(&s);
    uint32_t t = T0;

    // Ride 100 m, stop 5 minutes at a cafe with GPS drift, ride 100 m
    double lat = 45;
    for (int i = 0; i <= 20; i++, t++) {
        GpsFix f = fix_at(t, lat + i * 5 / METERS_PER_DEG_LAT, 7, 5, 100);
        ride_stats_update(&s, &f);
    }
    lat += 100 / METERS_PER_DEG_LAT;
    for (int i = 0; i < 300; i++, t++) {
        GpsFix f = fix_at(t, lat + noise(i, 5) / METERS_PER_DEG_LAT, 7, 0.4f, 100);
        ride_stats_update(&s, &f);
    }
    for (int i = 1; i <= 20; i++, t++) {
        GpsFix f = fix_at(t, lat + i * 5 / METERS_PER_DEG_LAT, 7, 5, 100);
        ride_stats_update(&s, &f);
    }

    TEST_ASSERT_DOUBLE_WITHIN(1, 200, s.distance_m);
    TEST_ASSERT_EQUAL_UINT32(40, s.moving_s);
    TEST_ASSERT_EQUAL_UINT32(340, ride_stats_elapsed_s(&s));
}

static void test_stats_ignores_invalid_and_glitches(void) {
    RideStats s;
    ride_stats_init(&s);

    GpsFix f = fix_at(T0, 45, 7, 5, 100);
    ride_stats_update(&s, &f);

    GpsFix lost = fix_at(T0 + 1, 0, 0, 0, 0);
    lost.valid = false;
    ride_stats_update(&s, &lost);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 0, s.distance_m);

    // Speed spike right after reacquiring
    f = fix_at(T0 + 2, 45 + 10 / METERS_PER_DEG_LAT, 7, 60, 100);
    ride_stats_update(&s, &f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0, s.max_speed_mps);

    // Same second again changes nothing
    ride_stats_update(&s, &f);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 10, s.distance_m);
    TEST_ASSERT_EQUAL_UINT32(2, s.moving_s);
}

static void test_stats_long_gap(void) {
    RideStats s;
    ride_stats_init(&s);
    GpsFix f = fix_at(T0, 45, 7, 5, 100);
    ride_stats_update(&s, &f);
    // Tunnel: no fix for two minutes, then 600 m further on
    f = fix_at(T0 + 120, 45 + 600 / METERS_PER_DEG_LAT, 7, 5, 100);
    ride_stats_update(&s, &f);
    TEST_ASSERT_DOUBLE_WITHIN(1, 600, s.distance_m);
    TEST_ASSERT_EQUAL_UINT32(RIDE_MAX_GAP_S, s.moving_s);
}

static void test_stats_pause(void) {
    RideStats s;
    ride_stats_init(&s);
    GpsFix f = fix_at(T0, 45, 7, 5, 100);
    ride_stats_update(&s, &f);
    f = fix_at(T0 + 10, 45 + 50 / METERS_PER_DEG_LAT, 7, 5, 100);
    ride_stats_update(&s, &f);

    // Pause, take a train 20 km and 200 m up, resume
    ride_stats_pause(&s);
    f = fix_at(T0 + 3600, 45 + 20000 / METERS_PER_DEG_LAT, 7, 5, 300);
    ride_stats_update(&s, &f);
    f = fix_at(T0 + 3610, 45 + 20050 / METERS_PER_DEG_LAT, 7, 5, 300);
    ride_stats_update(&s, &f);

    TEST_ASSERT_DOUBLE_WITHIN(1, 100, s.distance_m);
    TEST_ASSERT_EQUAL_UINT32(20, s.moving_s);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0, s.ele_gain_m);
    TEST_ASSERT_EQUAL_UINT32(3610, ride_stats_elapsed_s(&s));  // elapsed includes the pause
}

static void test_elevation_noise_on_flat(void) {
    RideStats s;
    ride_stats_init(&s);
    // An hour on the flat, altitude jumping +-2 m every second
    for (uint32_t i = 0; i <= 3600; i++) {
        GpsFix f = fix_at(T0 + i, 45 + i * 5 / METERS_PER_DEG_LAT, 7, 5, 100 + noise(i, 2));
        ride_stats_update(&s, &f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0, s.ele_gain_m);
}

static void test_elevation_hills(void) {
    RideStats s;
    ride_stats_init(&s);
    uint32_t t = T0;
    // Two hills: up 50 m, down 30 m, up 40 m, with +-2 m noise. Real gain 90 m.
    const float legs[] = {50, -30, 40};
    float ele = 100;
    for (float leg : legs) {
        for (int i = 0; i < 300; i++, t++) {
            ele += leg / 300;
            GpsFix f = fix_at(t, 45 + (t - T0) * 5 / METERS_PER_DEG_LAT, 7, 5, ele + noise(t, 2));
            ride_stats_update(&s, &f);
        }
    }
    // Hysteresis may undercount by up to the threshold per dip, plus the
    // smoothing lag at the top
    TEST_ASSERT_FLOAT_WITHIN(2 * RIDE_ELE_THRESHOLD_M + 2, 90 - RIDE_ELE_THRESHOLD_M, s.ele_gain_m);
    TEST_ASSERT_TRUE(s.ele_gain_m <= 90.5f);
}

// ---- GPX --------------------------------------------------------------------

static void test_gpx(void) {
    char buf[GPX_HEADER_MAX];

    size_t n = gpx_header(buf, sizeof(buf), T0);
    TEST_ASSERT_EQUAL(strlen(buf), n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "<gpx version=\"1.1\" creator=\"Coaster\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<metadata><time>2026-11-03T07:15:42Z</time></metadata>"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<trk><type>cycling</type><trkseg>\n"));

    GpsFix f = fix_at(T0 + 1, 45.1234567, -7.654321, 5, 123.3f);
    n = gpx_point(buf, GPX_POINT_MAX, &f);
    TEST_ASSERT_EQUAL_STRING("<trkpt lat=\"45.123457\" lon=\"-7.654321\"><ele>123.3</ele>"
                             "<time>2026-11-03T07:15:43Z</time></trkpt>\n",
                             buf);
    TEST_ASSERT_EQUAL(strlen(buf), n);

    // Worst case still fits in GPX_POINT_MAX
    f = fix_at(4000000000u, -89.9999999, -179.9999999, 0, -12345.6f);
    TEST_ASSERT_TRUE(gpx_point(buf, GPX_POINT_MAX, &f) > 0);

    TEST_ASSERT_EQUAL(0, gpx_point(buf, 20, &f));  // too small

    n = gpx_footer(buf, GPX_FOOTER_MAX);
    TEST_ASSERT_EQUAL_STRING("</trkseg></trk>\n</gpx>\n", buf);

    gpx_new_segment(buf, GPX_SEGMENT_MAX);
    TEST_ASSERT_EQUAL_STRING("</trkseg><trkseg>\n", buf);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_time);
    RUN_TEST(test_nmea_fix);
    RUN_TEST(test_nmea_order_and_noise);
    RUN_TEST(test_nmea_bad_checksum);
    RUN_TEST(test_nmea_mismatched_seconds);
    RUN_TEST(test_nmea_no_fix);
    RUN_TEST(test_nmea_gn_talker);
    RUN_TEST(test_distance);
    RUN_TEST(test_stats_steady_ride);
    RUN_TEST(test_stats_stop_adds_no_distance);
    RUN_TEST(test_stats_ignores_invalid_and_glitches);
    RUN_TEST(test_stats_long_gap);
    RUN_TEST(test_stats_pause);
    RUN_TEST(test_elevation_noise_on_flat);
    RUN_TEST(test_elevation_hills);
    RUN_TEST(test_gpx);
    return UNITY_END();
}
