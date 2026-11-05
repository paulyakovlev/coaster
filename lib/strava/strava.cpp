#include "strava.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The value after "key":, skipping spaces, or nullptr
static const char *find_value(const char *json, const char *key) {
    char pattern[40];
    int n = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (n <= 0 || n >= (int)sizeof(pattern)) return nullptr;
    for (const char *p = strstr(json, pattern); p; p = strstr(p + 1, pattern)) {
        const char *v = p + n;
        while (*v == ' ') v++;
        if (*v != ':') continue;  // the key's text appeared as a value
        v++;
        while (*v == ' ') v++;
        return v;
    }
    return nullptr;
}

bool strava_json_str(const char *json, const char *key, char *out, size_t size) {
    const char *v = find_value(json, key);
    if (!v || *v != '"' || size == 0) return false;
    size_t len = 0;
    for (v++; *v && *v != '"'; v++) {
        if (*v == '\\' && v[1]) v++;  // \" and \\ keep the next char; others stay rough
        if (len + 1 >= size) return false;
        out[len++] = *v;
    }
    if (*v != '"') return false;  // reply cut off
    out[len] = '\0';
    return true;
}

bool strava_json_int(const char *json, const char *key, long long *out) {
    const char *v = find_value(json, key);
    if (!v) return false;
    char *end;
    long long n = strtoll(v, &end, 10);
    if (end == v) return false;  // null or not a number
    *out = n;
    return true;
}

bool strava_scope_ok(const char *scope) {
    // Comma separated, e.g. "read,activity:write"
    for (const char *p = strstr(scope, "activity:write"); p; p = strstr(p + 1, "activity:write")) {
        bool start = p == scope || p[-1] == ',' || p[-1] == ' ';
        char after = p[strlen("activity:write")];
        if (start && (after == '\0' || after == ',' || after == ' ')) return true;
    }
    return false;
}

StravaUpload strava_upload_status(const char *json) {
    long long activity;
    if (strava_json_int(json, "activity_id", &activity)) return STRAVA_UP_DONE;
    // "error" is null while processing; a duplicate names the existing activity
    const char *v = find_value(json, "error");
    if (!v || *v != '"') return STRAVA_UP_PROCESSING;
    char error[512];
    if (strava_json_str(json, "error", error, sizeof(error)) && strstr(error, "duplicate of")) {
        return STRAVA_UP_DUPLICATE;
    }
    return STRAVA_UP_REJECTED;
}
