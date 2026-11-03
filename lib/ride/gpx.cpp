#include "ride.h"

#include <stdio.h>

// snprintf result -> length, or 0 if it didn't fit
static size_t fit(int n, size_t size) {
    return (n > 0 && (size_t)n < size) ? (size_t)n : 0;
}

// No track name: Strava names the ride itself ("Morning Ride").
size_t gpx_header(char *buf, size_t size, uint32_t start_time) {
    char t[21];
    unix_to_iso8601(start_time, t);
    int n = snprintf(buf, size,
                     "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                     "<gpx version=\"1.1\" creator=\"Coaster\" "
                     "xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
                     "<metadata><time>%s</time></metadata>\n"
                     "<trk><type>cycling</type><trkseg>\n",
                     t);
    return fit(n, size);
}

size_t gpx_point(char *buf, size_t size, const GpsFix *fix) {
    char t[21];
    unix_to_iso8601(fix->time, t);
    int n = snprintf(buf, size,
                     "<trkpt lat=\"%.6f\" lon=\"%.6f\"><ele>%.1f</ele><time>%s</time></trkpt>\n",
                     fix->lat, fix->lon, fix->ele_m, t);
    return fit(n, size);
}

size_t gpx_footer(char *buf, size_t size) {
    return fit(snprintf(buf, size, "</trkseg></trk>\n</gpx>\n"), size);
}

size_t gpx_new_segment(char *buf, size_t size) {
    return fit(snprintf(buf, size, "</trkseg><trkseg>\n"), size);
}

// Days since 1970-01-01 for a civil date, and back. Howard Hinnant's
// algorithms (http://howardhinnant.github.io/date_algorithms.html).
static int32_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int32_t  era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);
    uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

static void civil_from_days(int32_t z, int *y, int *m, int *d) {
    z += 719468;
    int32_t  era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp  = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)yoe + era * 400 + (*m <= 2);
}

uint32_t utc_to_unix(int year, int month, int day, int hour, int min, int sec) {
    return (uint32_t)days_from_civil(year, month, day) * 86400u + hour * 3600 + min * 60 + sec;
}

void unix_to_iso8601(uint32_t t, char *buf) {
    int y, m, d;
    civil_from_days((int32_t)(t / 86400), &y, &m, &d);
    uint32_t s = t % 86400;
    snprintf(buf, 21, "%04d-%02d-%02dT%02lu:%02lu:%02luZ", y, m, d,
             (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
}
