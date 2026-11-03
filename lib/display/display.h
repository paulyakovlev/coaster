#pragma once

#include <Adafruit_GFX.h>

// Glue between a lib/screen canvas and the Waveshare e-paper panel. The only
// board-specific display code outside lib/epaper_driver; swap this for v1.
//
// The vendor driver always sends the whole 200x200 buffer. A partial refresh
// uses the partial waveform, so only pixels that changed actually move.

// Power the panel and do a first full refresh with the canvas.
void display_begin(GFXcanvas1 &canvas);

// Full refresh (about 2 s, flashes). Clears ghosting.
void display_full(GFXcanvas1 &canvas);

// Partial refresh. Fast, no flash, but ghosting builds up.
void display_partial(GFXcanvas1 &canvas);
