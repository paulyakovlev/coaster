#include "ride.h"

#include <math.h>
#include <string.h>

#define EARTH_RADIUS_M 6371000.0
#define DEG_TO_RAD     (M_PI / 180.0)

double ride_distance_m(double lat1, double lon1, double lat2, double lon2) {
    double dlat = (lat2 - lat1) * DEG_TO_RAD;
    double dlon = (lon2 - lon1) * DEG_TO_RAD;
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * sin(dlon / 2) * sin(dlon / 2);
    return 2 * EARTH_RADIUS_M * asin(sqrt(a));
}

float pressure_to_altitude_m(float pressure_pa, float sea_level_pa) {
    return 44330.0f * (1.0f - powf(pressure_pa / sea_level_pa, 0.190295f));
}

void ride_stats_init(RideStats *s) {
    memset(s, 0, sizeof(*s));
}

static void update_elevation(RideStats *s, float ele) {
    s->ele_m += RIDE_ELE_SMOOTHING * (ele - s->ele_m);

    // Hysteresis: count a climb only once it clears the threshold, and move
    // the reference down only once a drop clears it too. Noise smaller than
    // the threshold never counts; each real dip may undercount by up to it.
    if (s->ele_m > s->ele_ref + RIDE_ELE_THRESHOLD_M) {
        s->ele_gain_m += s->ele_m - s->ele_ref;
        s->ele_ref = s->ele_m;
    } else if (s->ele_m < s->ele_ref - RIDE_ELE_THRESHOLD_M) {
        s->ele_ref = s->ele_m;
    }
}

void ride_stats_update(RideStats *s, const GpsFix *fix) {
    if (!fix->valid || fix->time == 0) return;

    if (!s->started) {
        s->started    = true;
        s->start_time = s->last_time = fix->time;
        s->anchor_lat = fix->lat;
        s->anchor_lon = fix->lon;
        s->ele_m      = s->ele_ref = fix->ele_m;
        return;
    }
    if (fix->time <= s->last_time) return;  // repeated or out of order

    if (s->reanchor) {
        s->reanchor   = false;
        s->last_time  = fix->time;
        s->anchor_lat = fix->lat;
        s->anchor_lon = fix->lon;
        s->speed_mps  = fix->speed_mps;
        s->ele_m      = s->ele_ref = fix->ele_m;
        return;
    }

    uint32_t dt = fix->time - s->last_time;
    s->last_time = fix->time;
    s->speed_mps = fix->speed_mps;
    update_elevation(s, fix->ele_m);

    // Stopped: keep the anchor where it is, so GPS drift adds no distance.
    // When riding resumes, the first step spans the whole stop.
    if (fix->speed_mps < RIDE_MOVING_MPS) return;

    s->distance_m += ride_distance_m(s->anchor_lat, s->anchor_lon, fix->lat, fix->lon);
    s->anchor_lat = fix->lat;
    s->anchor_lon = fix->lon;
    s->moving_s += dt < RIDE_MAX_GAP_S ? dt : RIDE_MAX_GAP_S;

    if (fix->speed_mps < RIDE_MAX_SPEED_MPS && fix->speed_mps > s->max_speed_mps) {
        s->max_speed_mps = fix->speed_mps;
    }
}

void ride_stats_pause(RideStats *s) {
    s->reanchor  = true;
    s->speed_mps = 0;
}

uint32_t ride_stats_elapsed_s(const RideStats *s) {
    return s->last_time - s->start_time;
}

float ride_stats_avg_speed_mps(const RideStats *s) {
    return s->moving_s ? (float)(s->distance_m / s->moving_s) : 0;
}
