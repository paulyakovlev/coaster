#pragma once

#include <stdint.h>

#include "ride.h"

// A made-up ride, one GPS fix per second, standing in for the GPS until the
// module is wired. Loops through a cruise, a climb, a fast descent and a
// stop, so every riding pose shows up within a few minutes.

struct FakeRide {
    uint32_t time;  // Unix seconds of the next fix
    uint32_t t;     // seconds since the start
    double   lat;
    float    ele;
};

void fake_ride_init(FakeRide *r, uint32_t start_time);

// The next fix, one second after the previous one
void fake_ride_next(FakeRide *r, GpsFix *fix);

// Length of one loop of the route, in seconds
uint32_t fake_ride_loop_s();
