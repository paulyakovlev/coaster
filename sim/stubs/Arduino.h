#pragma once

// Minimal stand-in for the Arduino core, just enough to compile Adafruit GFX
// and lib/screen on a desktop machine.

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#define PROGMEM

#define radians(deg) ((deg) * M_PI / 180.0)
#define degrees(rad) ((rad) * 180.0 / M_PI)

typedef uint8_t byte;
typedef bool boolean;

class __FlashStringHelper;
#define F(s) (reinterpret_cast<const __FlashStringHelper *>(s))

class String {
public:
    String(const char *s = "") : s_(s) {}
    const char *c_str() const { return s_.c_str(); }
    unsigned int length() const { return s_.length(); }

private:
    std::string s_;
};

#include "Print.h"
