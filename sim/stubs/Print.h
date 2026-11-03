#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Stand-in for the Arduino Print class: formats values and sends each byte
// to write(), which Adafruit_GFX implements to draw characters.
class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;

    size_t write(const uint8_t *buf, size_t len) {
        size_t n = 0;
        while (len--) n += write(*buf++);
        return n;
    }
    size_t write(const char *s) { return write((const uint8_t *)s, strlen(s)); }

    size_t print(const char *s) { return write(s); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(int v) { return printf_("%d", v); }
    size_t print(unsigned int v) { return printf_("%u", v); }
    size_t print(long v) { return printf_("%ld", v); }
    size_t print(unsigned long v) { return printf_("%lu", v); }
    size_t print(double v, int digits = 2) { return printf_("%.*f", digits, v); }

    size_t println() { return write((uint8_t)'\n'); }
    template <typename T> size_t println(T v) { return print(v) + println(); }

private:
    template <typename... Args> size_t printf_(const char *fmt, Args... args) {
        char buf[32];
        snprintf(buf, sizeof(buf), fmt, args...);
        return write(buf);
    }
};
