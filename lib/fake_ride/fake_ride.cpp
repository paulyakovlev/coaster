#include "fake_ride.h"

#define METERS_PER_DEG_LAT 111195.0

struct Leg {
    uint16_t seconds;
    float    kmh;
    float    grade;  // rise over run
};

static const Leg ROUTE[] = {
    {60, 22, 0},       // cruise
    {90, 11, 0.06f},   // climb
    {45, 38, -0.05f},  // fast descent
    {25, 0, 0},        // stop at the bottom
    {60, 20, 0.01f},   // cruise home
};

void fake_ride_init(FakeRide *r, uint32_t start_time) {
    r->time = start_time;
    r->t    = 0;
    r->lat  = 45.0;
    r->ele  = 120.0f;
}

uint32_t fake_ride_loop_s() {
    uint32_t total = 0;
    for (const Leg &leg : ROUTE) total += leg.seconds;
    return total;
}

void fake_ride_next(FakeRide *r, GpsFix *fix) {
    uint32_t s = r->t % fake_ride_loop_s();
    const Leg *leg = &ROUTE[0];
    for (const Leg &l : ROUTE) {
        leg = &l;
        if (s < l.seconds) break;
        s -= l.seconds;
    }

    float mps = leg->kmh / 3.6f;
    r->lat += mps / METERS_PER_DEG_LAT;
    r->ele += mps * leg->grade;

    fix->time      = r->time;
    fix->lat       = r->lat;
    fix->lon       = 7.0;
    fix->speed_mps = mps;
    fix->ele_m     = r->ele;
    fix->hdop      = 1.0f;
    fix->sats      = 8;
    fix->valid     = true;

    r->time++;
    r->t++;
}
