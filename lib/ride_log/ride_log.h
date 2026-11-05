#pragma once

#include <stdint.h>
#include <stdio.h>

#include "ride.h"

// Rides stored as GPX files, one per ride, in a directory. Plain C stdio,
// so the same code runs on LittleFS on the board and on a temp directory in
// the unit tests.
//
// While recording, the file is "<start>.part" ("20261103-071542.part",
// from the first fix's UTC time). Finishing adds the footer and renames it
// to "<start>.gpx". A .part file left by a power cut is repaired at boot by
// ride_log_recover.

// Sync to flash every this many points: a power cut loses at most this many
// seconds. shortcut: chosen without measuring flash wear; revisit with the
// battery test.
#define RIDE_LOG_SYNC_EVERY 10

#define RIDE_LOG_PATH_MAX   128

struct RideLog {
    const char *dir;
    FILE       *f;              // open from the first fix to the finish
    char        path[RIDE_LOG_PATH_MAX];  // the .part file
    uint32_t    points;
    uint16_t    unsynced;
    bool        new_segment;    // paused: next point starts a new segment
    bool        failed;         // a write failed; the rest of the ride is lost
};

// A new ride. Nothing is written until the first fix with a position.
void ride_log_start(RideLog *log, const char *dir);

// One point. Opens the file on the first one. Returns false if writing failed.
bool ride_log_point(RideLog *log, const GpsFix *fix);

// Paused by hand: sync now, and start a new segment at the next point
bool ride_log_pause(RideLog *log);

// Footer, close, rename to .gpx. True if the ride is safely stored (or there
// was nothing to store: no fix all ride).
bool ride_log_finish(RideLog *log);

// Repair every .part file in dir: keep the complete lines, add the footer and
// save it as .gpx. Files cut off inside the header are deleted (no points).
// Returns how many rides were repaired, or -1 if one couldn't be.
int ride_log_recover(const char *dir);
