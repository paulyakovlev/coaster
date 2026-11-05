// Unit tests for lib/strava: reading Strava's replies.
// Run: pio test -e native

#include <string.h>
#include <unity.h>

#include "strava.h"

void setUp() {}
void tearDown() {}

// Shaped like the POST /oauth/token reply in developers.strava.com/docs/authentication
static const char TOKEN[] =
    "{\"token_type\":\"Bearer\",\"expires_at\":1793712863,\"expires_in\":21600,"
    "\"refresh_token\":\"e5n567567abc\",\"access_token\":\"a4b945687g9\","
    "\"athlete\":{\"id\":123,\"username\":\"rider \\\"one\\\"\",\"profile\":null}}";

static void test_token_reply(void) {
    char s[32];
    TEST_ASSERT_TRUE(strava_json_str(TOKEN, "access_token", s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("a4b945687g9", s);
    TEST_ASSERT_TRUE(strava_json_str(TOKEN, "refresh_token", s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("e5n567567abc", s);
    long long n;
    TEST_ASSERT_TRUE(strava_json_int(TOKEN, "expires_at", &n));
    TEST_ASSERT_EQUAL(1793712863LL, n);
}

static void test_strings_edge_cases(void) {
    char s[32];
    // Escaped quotes
    TEST_ASSERT_TRUE(strava_json_str(TOKEN, "username", s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("rider \"one\"", s);
    // null, missing, a number, too long, cut off
    TEST_ASSERT_FALSE(strava_json_str(TOKEN, "profile", s, sizeof(s)));
    TEST_ASSERT_FALSE(strava_json_str(TOKEN, "missing", s, sizeof(s)));
    TEST_ASSERT_FALSE(strava_json_str(TOKEN, "expires_in", s, sizeof(s)));
    TEST_ASSERT_FALSE(strava_json_str(TOKEN, "access_token", s, 5));
    TEST_ASSERT_FALSE(strava_json_str("{\"a\":\"abc", "a", s, sizeof(s)));
    // Spaces around the colon; a key's name appearing as a value first
    TEST_ASSERT_TRUE(strava_json_str("{\"x\":\"a\", \"a\" : \"b\"}", "a", s, sizeof(s)));
    TEST_ASSERT_EQUAL_STRING("b", s);
    long long n;
    TEST_ASSERT_FALSE(strava_json_int(TOKEN, "profile", &n));
}

static void test_scope(void) {
    TEST_ASSERT_TRUE(strava_scope_ok("read,activity:write"));
    TEST_ASSERT_TRUE(strava_scope_ok("activity:write"));
    TEST_ASSERT_TRUE(strava_scope_ok("activity:write,read"));
    TEST_ASSERT_FALSE(strava_scope_ok("read"));
    TEST_ASSERT_FALSE(strava_scope_ok("read,activity:read_all"));
    TEST_ASSERT_FALSE(strava_scope_ok(""));
}

// Shaped like Strava's upload replies; the "ready" one matches a real reply from the board
static void test_upload_status(void) {
    TEST_ASSERT_EQUAL(STRAVA_UP_PROCESSING,
                      strava_upload_status("{\"id\":16486788,\"id_str\":\"16486788\","
                                           "\"external_id\":\"20261103-071542.gpx\","
                                           "\"error\":null,\"status\":\"Your activity is still "
                                           "being processed.\",\"activity_id\":null}"));
    TEST_ASSERT_EQUAL(STRAVA_UP_DONE,
                      strava_upload_status("{\"id\":16486788,\"error\":null,"
                                           "\"status\":\"Your activity is ready.\","
                                           "\"activity_id\":1234567890123}"));
    TEST_ASSERT_EQUAL(STRAVA_UP_DUPLICATE,
                      strava_upload_status("{\"id\":16486788,\"error\":\"20261103-071542.gpx "
                                           "duplicate of <a href='/activities/12'>Morning Ride"
                                           "</a>\",\"status\":\"There was an error processing "
                                           "your activity.\",\"activity_id\":null}"));
    TEST_ASSERT_EQUAL(STRAVA_UP_REJECTED,
                      strava_upload_status("{\"id\":16486788,\"error\":\"Time information is "
                                           "missing from file.\",\"status\":\"There was an "
                                           "error processing your activity.\","
                                           "\"activity_id\":null}"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_token_reply);
    RUN_TEST(test_strings_edge_cases);
    RUN_TEST(test_scope);
    RUN_TEST(test_upload_status);
    return UNITY_END();
}
