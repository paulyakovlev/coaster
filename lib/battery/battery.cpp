#include "battery.h"

// Typical one-cell LiPo discharge curve at light load: (mV, %), falling.
// Linear between points.
static const struct {
    uint16_t mv;
    uint8_t  pct;
} CURVE[] = {
    {4200, 100}, {4100, 90}, {4000, 78}, {3900, 63}, {3800, 42},
    {3750, 30},  {3700, 17}, {3600, 6},  {3300, 0},
};
#define CURVE_LEN (sizeof(CURVE) / sizeof(CURVE[0]))

uint8_t battery_percent(uint16_t mv) {
    if (mv >= CURVE[0].mv) return 100;
    for (unsigned i = 1; i < CURVE_LEN; i++) {
        if (mv >= CURVE[i].mv) {
            uint16_t span = CURVE[i - 1].mv - CURVE[i].mv;
            uint16_t rise = CURVE[i - 1].pct - CURVE[i].pct;
            return CURVE[i].pct + (uint8_t)((uint32_t)(mv - CURVE[i].mv) * rise / span);
        }
    }
    return 0;
}
