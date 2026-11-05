#pragma once

#include <stdint.h>

// Charge left in a one-cell LiPo, 0-100, from its voltage in mV.
// shortcut: generic resting-voltage curve, reads high while charging and low
// under load; calibrate with a discharge test on the real battery.
uint8_t battery_percent(uint16_t mv);
