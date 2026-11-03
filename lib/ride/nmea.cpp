#include "ride.h"

#include <stdlib.h>
#include <string.h>

#define KNOTS_TO_MPS 0.514444f
#define MAX_FIELDS   20

void nmea_init(NmeaParser *p) {
    memset(p, 0, sizeof(*p));
    p->rmc_tod = -1;
    p->gga_tod = -1;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// line holds everything after "$". Checks "*hh" and cuts it off.
static bool checksum_ok(char *line) {
    char *star = strchr(line, '*');
    if (!star || strlen(star) < 3) return false;
    int hi = hex_digit(star[1]), lo = hex_digit(star[2]);
    if (hi < 0 || lo < 0) return false;

    uint8_t sum = 0;
    for (char *c = line; c < star; c++) sum ^= (uint8_t)*c;
    *star = '\0';
    return sum == (hi << 4 | lo);
}

// Split on commas in place. Empty fields become empty strings.
static int split(char *line, char **fields) {
    int n = 0;
    fields[n++] = line;
    for (char *c = line; *c && n < MAX_FIELDS; c++) {
        if (*c == ',') {
            *c = '\0';
            fields[n++] = c + 1;
        }
    }
    return n;
}

// "hhmmss.ss" -> hhmmss as an integer, -1 if empty
static int32_t time_of_day(const char *f) {
    if (strlen(f) < 6) return -1;
    return atol(f) % 1000000;
}

// "ddmm.mmmm" or "dddmm.mmmm" plus hemisphere -> degrees
static double coord(const char *value, const char *hemi) {
    double v = strtod(value, nullptr);
    int deg = (int)(v / 100);
    double d = deg + (v - deg * 100) / 60.0;
    return (hemi[0] == 'S' || hemi[0] == 'W') ? -d : d;
}

// The other half of the fix is for a different second: drop it
static void start_second(NmeaParser *p, int32_t tod) {
    if (p->rmc_tod != tod && p->gga_tod != tod) {
        p->rmc_tod = p->gga_tod = -1;
        p->rmc_ok = p->gga_ok = false;
    }
}

static void parse_rmc(NmeaParser *p, char **f, int n) {
    if (n < 10) return;
    int32_t tod = time_of_day(f[1]);
    if (tod < 0) return;
    start_second(p, tod);

    GpsFix *fix = &p->pending;
    p->rmc_ok = f[2][0] == 'A';
    if (p->rmc_ok) {
        fix->lat = coord(f[3], f[4]);
        fix->lon = coord(f[5], f[6]);
        fix->speed_mps = strtof(f[7], nullptr) * KNOTS_TO_MPS;
    }

    // ddmmyy; the GPS may know the time before it has a position
    fix->time = 0;
    if (strlen(f[9]) == 6) {
        long date = atol(f[9]);
        int  yy   = date % 100;
        fix->time = utc_to_unix(yy < 80 ? 2000 + yy : 1900 + yy, date / 100 % 100, date / 10000,
                                tod / 10000, tod / 100 % 100, tod % 100);
    }
    p->rmc_tod = tod;
}

static void parse_gga(NmeaParser *p, char **f, int n) {
    if (n < 10) return;
    int32_t tod = time_of_day(f[1]);
    if (tod < 0) return;
    start_second(p, tod);

    GpsFix *fix = &p->pending;
    p->gga_ok = atoi(f[6]) > 0;  // fix quality, 0 = none
    fix->sats = (uint8_t)atoi(f[7]);
    fix->hdop = strtof(f[8], nullptr);
    if (p->gga_ok) fix->ele_m = strtof(f[9], nullptr);
    p->gga_tod = tod;
}

// Returns true if this sentence completed a fix
static bool parse_line(NmeaParser *p) {
    if (!checksum_ok(p->line)) return false;

    char *f[MAX_FIELDS];
    int n = split(p->line, f);
    // Talker is the first two letters ("GP", "GN"); the type follows
    if (strlen(f[0]) != 5) return false;
    const char *type = f[0] + 2;
    if (strcmp(type, "RMC") == 0) {
        parse_rmc(p, f, n);
    } else if (strcmp(type, "GGA") == 0) {
        parse_gga(p, f, n);
    } else {
        return false;
    }
    return p->rmc_tod >= 0 && p->rmc_tod == p->gga_tod;
}

bool nmea_feed(NmeaParser *p, char c, GpsFix *out) {
    if (c == '$') {
        p->in_sentence = true;
        p->len = 0;
        return false;
    }
    if (!p->in_sentence) return false;

    if (c == '\r' || c == '\n') {
        p->in_sentence = false;
        p->line[p->len] = '\0';
        if (!parse_line(p)) return false;

        p->pending.valid = p->rmc_ok && p->gga_ok;
        *out = p->pending;
        p->rmc_tod = p->gga_tod = -1;
        return true;
    }

    if (p->len >= NMEA_MAX_LINE) {
        p->in_sentence = false;  // too long: garbage, wait for the next "$"
        return false;
    }
    p->line[p->len++] = c;
    return false;
}
