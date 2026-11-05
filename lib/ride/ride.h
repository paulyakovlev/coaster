#pragma once

#include <stddef.h>
#include <stdint.h>

// Ride recording, independent of the hardware: GPS sentences in, ride stats
// and GPX text out. No allocation; the caller owns every struct and buffer.
// Builds for the simulator and the unit tests in test/.

// ---- GPS fix ----------------------------------------------------------------

struct GpsFix {
    uint32_t time;       // Unix seconds, UTC. 0 until the GPS knows the date.
    double   lat, lon;   // degrees, north and east positive
    float    speed_mps;  // ground speed from the GPS (Doppler, not position deltas)
    float    ele_m;      // GPS altitude above sea level; the barometer will replace it
    float    hdop;       // horizontal dilution of precision, lower is better
    uint8_t  sats;
    bool     valid;      // position fix; when false only time may be set
};

// ---- NMEA parser --------------------------------------------------------------

// Reads the GPS UART byte by byte. A fix is complete once the RMC sentence
// (date, position, speed) and GGA sentence (altitude, satellites) for the
// same second have both arrived; GPS modules send both once per second.
// Accepts any talker (GP, GN, ...). Sentences with a bad checksum are dropped.

#define NMEA_MAX_LINE 82  // NMEA 0183 limit, including "$" and "*hh"

struct NmeaParser {
    char     line[NMEA_MAX_LINE + 1];
    uint8_t  len;
    bool     in_sentence;

    GpsFix   pending;
    int32_t  rmc_tod, gga_tod;  // time of day (hhmmss) of each half, -1 if none
    bool     rmc_ok, gga_ok;    // each half reported a position fix
};

void nmea_init(NmeaParser *p);

// Feed one byte. Returns true when a fix is complete; it is copied to *out.
bool nmea_feed(NmeaParser *p, char c, GpsFix *out);

// ---- Ride stats ---------------------------------------------------------------

// Speed below this counts as stopped: no distance, no moving time. GPS drift
// while standing still reads as 1-2 km/h.
#define RIDE_MOVING_MPS     (3.0f / 3.6f)

// Speeds above this are GPS glitches (often right after a lost fix).
#define RIDE_MAX_SPEED_MPS  (120.0f / 3.6f)

// Elevation: smooth, then only count a climb once it passes this threshold,
// so altitude noise on the flat doesn't add up to fake climbing.
#define RIDE_ELE_SMOOTHING  0.1f   // about a 10 s average
#define RIDE_ELE_THRESHOLD_M 3.0f

// Gaps in fixes longer than this (tunnel, lost signal) count this much
// moving time, not the whole gap.
#define RIDE_MAX_GAP_S      10

struct RideStats {
    uint32_t start_time;     // first valid fix
    uint32_t last_time;      // latest valid fix
    double   distance_m;
    uint32_t moving_s;
    float    max_speed_mps;
    float    speed_mps;      // latest, for the screen
    float    ele_m;          // smoothed, for the screen
    float    ele_gain_m;

    // Internal
    bool     started;
    bool     reanchor;               // paused: next fix starts fresh
    double   anchor_lat, anchor_lon;  // last point distance was measured from
    float    ele_ref;                 // low point the next climb counts from
};

void ride_stats_init(RideStats *s);

// Add one fix. Invalid fixes are ignored.
void ride_stats_update(RideStats *s, const GpsFix *fix);

// The rider paused by hand. Don't feed fixes while paused; the first fix
// after resuming adds no distance, moving time or climbing, so a train ride
// or a lift during the pause doesn't count.
void ride_stats_pause(RideStats *s);

uint32_t ride_stats_elapsed_s(const RideStats *s);

// Average over moving time, like Strava
float ride_stats_avg_speed_mps(const RideStats *s);

// Great-circle distance in meters
double ride_distance_m(double lat1, double lon1, double lat2, double lon2);

// ---- Barometric altitude ------------------------------------------------------

// Standard sea-level pressure. The real value moves with the weather by up to
// a few kPa, which shifts absolute altitude by tens of meters but barely
// changes elevation gain over a ride.
#define SEA_LEVEL_PA 101325.0f

// International barometric formula (standard atmosphere, troposphere).
// To use the barometer for elevation, put this into GpsFix.ele_m before
// ride_stats_update().
float pressure_to_altitude_m(float pressure_pa, float sea_level_pa);

// ---- GPX ------------------------------------------------------------------------

// GPX 1.1, written as the ride goes: header once, one point per second,
// footer at the end. Each point is one line, so a file cut off by power loss
// is repaired by trimming a partial last line and appending the footer.
//
// Each function writes into buf and returns the length, or 0 if it didn't fit.

#define GPX_HEADER_MAX 320
#define GPX_POINT_MAX  128
#define GPX_FOOTER_MAX 32
#define GPX_SEGMENT_MAX 24

size_t gpx_header(char *buf, size_t size, uint32_t start_time);
size_t gpx_point(char *buf, size_t size, const GpsFix *fix);
size_t gpx_footer(char *buf, size_t size);

// After a pause: close the track segment and open a new one, so the gap
// isn't drawn as a straight line
size_t gpx_new_segment(char *buf, size_t size);

// ---- Time -------------------------------------------------------------------

uint32_t utc_to_unix(int year, int month, int day, int hour, int min, int sec);

// ISO 8601, "2026-11-03T07:15:42Z". buf needs 21 bytes.
void unix_to_iso8601(uint32_t t, char *buf);
