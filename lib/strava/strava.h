#pragma once

#include <stddef.h>

// Reading Strava's replies, independent of the hardware. The network side
// (TLS, Wi-Fi, NVS) is in src/sync.cpp.
//
// Not a JSON parser: finds "key": value at any depth. Fine for the few flat
// keys we read from replies whose shape Strava documents.

// "key": "text" -> text, with \" and \\ unescaped. False if the key is
// missing, null, not a string, or the text doesn't fit.
bool strava_json_str(const char *json, const char *key, char *out, size_t size);

// "key": 123 -> 123. False if missing, null or not a number.
bool strava_json_int(const char *json, const char *key, long long *out);

// The scope Strava granted (from the sign-in redirect) allows uploads. The
// rider can untick it on Strava's page.
bool strava_scope_ok(const char *scope);

// Where an upload stands, from the reply to POST or GET /uploads
enum StravaUpload {
    STRAVA_UP_PROCESSING,  // no activity yet: ask again
    STRAVA_UP_DONE,        // activity created
    STRAVA_UP_DUPLICATE,   // already on Strava: counts as done
    STRAVA_UP_REJECTED,    // Strava won't take the file
};
StravaUpload strava_upload_status(const char *json);
