#pragma once

#include <stdint.h>

// Write an 8-bit grayscale image (one byte per pixel, rows top to bottom).
bool png_write_gray(const char *path, const uint8_t *pixels, int width, int height);
