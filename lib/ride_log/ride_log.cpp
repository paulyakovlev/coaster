#include "ride_log.h"

#include <dirent.h>
#include <string.h>
#include <unistd.h>

// GPX lines are formatted here before writing. The header is the longest.
static char line[GPX_HEADER_MAX];

static bool put(RideLog *log, size_t len) {
    if (len == 0 || fwrite(line, 1, len, log->f) != len) log->failed = true;
    return !log->failed;
}

// Through the C library's buffer and the filesystem's cache onto the flash
static bool sync_file(FILE *f) {
    return fflush(f) == 0 && fsync(fileno(f)) == 0;
}

static bool sync_log(RideLog *log) {
    log->unsynced = 0;
    if (!sync_file(log->f)) log->failed = true;
    return !log->failed;
}

void ride_log_start(RideLog *log, const char *dir) {
    memset(log, 0, sizeof(*log));
    log->dir = dir;
}

// "2026-11-03T07:15:42Z" -> "<dir>/20261103-071542.part"
static bool open_file(RideLog *log, uint32_t start_time) {
    char t[21];
    unix_to_iso8601(start_time, t);
    int n = snprintf(log->path, sizeof(log->path), "%s/%.4s%.2s%.2s-%.2s%.2s%.2s.part", log->dir,
                     t, t + 5, t + 8, t + 11, t + 14, t + 17);
    if (n <= 0 || (size_t)n >= sizeof(log->path)) return false;
    log->f = fopen(log->path, "w");
    if (!log->f) return false;
    return put(log, gpx_header(line, sizeof(line), start_time)) && sync_log(log);
}

bool ride_log_point(RideLog *log, const GpsFix *fix) {
    if (log->failed) return false;
    if (!fix->valid || fix->time == 0) return true;  // nothing to record yet
    if (!log->f && !open_file(log, fix->time)) {
        log->failed = true;
        return false;
    }

    if (log->new_segment && !put(log, gpx_new_segment(line, sizeof(line)))) return false;
    log->new_segment = false;
    if (!put(log, gpx_point(line, sizeof(line), fix))) return false;
    log->points++;
    if (++log->unsynced >= RIDE_LOG_SYNC_EVERY) return sync_log(log);
    return true;
}

bool ride_log_pause(RideLog *log) {
    if (!log->f || log->failed) return !log->failed;
    log->new_segment = true;
    return sync_log(log);
}

// "<dir>/x.part" -> "<dir>/x.gpx"
static bool gpx_path(const char *part, char *out, size_t size) {
    size_t len = strlen(part);
    if (len < 5 || len - 5 + 5 >= size) return false;
    memcpy(out, part, len - 5);
    strcpy(out + len - 5, ".gpx");
    return true;
}

bool ride_log_finish(RideLog *log) {
    if (!log->f) return !log->failed;  // no fix all ride: nothing to store
    bool ok = !log->failed && put(log, gpx_footer(line, sizeof(line))) && sync_log(log);
    ok = fclose(log->f) == 0 && ok;
    log->f = nullptr;
    // A failed ride stays .part: the next boot repairs what reached the flash
    if (!ok) return false;

    char gpx[RIDE_LOG_PATH_MAX];
    return gpx_path(log->path, gpx, sizeof(gpx)) && rename(log->path, gpx) == 0;
}

// ---- Recovery after a power cut ---------------------------------------------

static bool ends_with(const char *name, const char *suffix) {
    size_t n = strlen(name), s = strlen(suffix);
    return n >= s && strcmp(name + n - s, suffix) == 0;
}

// The first .part file in dir, or false if there is none
static bool find_part(const char *dir, char *path, size_t size) {
    DIR *d = opendir(dir);
    if (!d) return false;
    bool found = false;
    for (struct dirent *e; !found && (e = readdir(d));) {
        if (!ends_with(e->d_name, ".part")) continue;
        int n = snprintf(path, size, "%s/%s", dir, e->d_name);
        found = n > 0 && (size_t)n < size;
    }
    closedir(d);
    return found;
}

// Copy the complete lines of a .part file into a .gpx and close it with the
// footer. The .part is deleted only once the .gpx is safely on the flash.
// Returns 1 if repaired, 0 if there was nothing to keep, -1 on failure.
static int repair(const char *part) {
    static char buf[512];
    FILE *in = fopen(part, "rb");
    if (!in) return -1;

    // Pass 1: where the last complete line ends, and whether the header made it
    long keep = 0, pos = 0;
    bool header = false;
    for (size_t n; (n = fread(buf, 1, sizeof(buf) - 1, in)) > 0; pos += (long)n) {
        if (pos == 0) {
            buf[n] = '\0';
            header = strstr(buf, "<trkseg>\n") != nullptr;
        }
        for (size_t i = 0; i < n; i++) {
            if (buf[i] == '\n') keep = pos + (long)i + 1;
        }
    }
    if (!header) {
        fclose(in);
        return remove(part) == 0 ? 0 : -1;
    }

    // Footer already there: the power cut came between finishing and the rename
    char footer[GPX_FOOTER_MAX];
    size_t footer_len = gpx_footer(footer, sizeof(footer));
    bool has_footer = keep == pos && pos >= (long)footer_len &&
                      fseek(in, pos - (long)footer_len, SEEK_SET) == 0 &&
                      fread(buf, 1, footer_len, in) == footer_len &&
                      memcmp(buf, footer, footer_len) == 0;

    // Pass 2: copy, then the footer
    char gpx[RIDE_LOG_PATH_MAX];
    FILE *out = gpx_path(part, gpx, sizeof(gpx)) ? fopen(gpx, "wb") : nullptr;
    if (!out) {
        fclose(in);
        return -1;
    }
    rewind(in);
    bool ok = true;
    for (long left = keep; ok && left > 0;) {
        size_t want = left < (long)sizeof(buf) ? (size_t)left : sizeof(buf);
        size_t n = fread(buf, 1, want, in);
        ok = n == want && fwrite(buf, 1, n, out) == n;
        left -= (long)n;
    }
    if (ok && !has_footer) ok = fwrite(footer, 1, footer_len, out) == footer_len;
    ok = ok && sync_file(out);
    ok = fclose(out) == 0 && ok;
    fclose(in);
    if (!ok) {
        remove(gpx);  // the .part still has everything
        return -1;
    }
    return remove(part) == 0 ? 1 : -1;
}

int ride_log_recover(const char *dir) {
    char part[RIDE_LOG_PATH_MAX];
    int repaired = 0;
    while (find_part(dir, part, sizeof(part))) {
        int r = repair(part);
        if (r < 0) return -1;  // stop: retrying the same file would loop
        repaired += r;
    }
    return repaired;
}
