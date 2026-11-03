# Coaster

A tiny e-ink cycling companion. It records rides to GPX and uploads them to Strava when it reaches home Wi-Fi. See [SPEC.md](SPEC.md) for what it does and what's out of scope.

## Hardware

- Waveshare ESP32-S3-ePaper-1.54 (200x200 black-and-white e-paper)
- NEO-6M class GPS (UART) and BMP280 class barometer (I2C)
- LiPo battery and microSD card

## Build and flash

Requires [PlatformIO](https://platformio.org/) (`brew install platformio`).

```sh
pio run                                    # build
pio run -t upload && pio device monitor    # flash and watch serial output
```

Refresh test (fake ride at 1 Hz, refresh timings and battery voltage over serial; see the top of `hwtest/refresh_test.cpp` for serial commands):

```sh
pio run -e refresh-test -t upload && pio device monitor
```

Barometer test (I2C scan, then altitude readings over serial):

```sh
pio run -e baro-test -t upload && pio device monitor
```

`platformio.ini` is set up for the V2 board (octal PSRAM, `qio_opi`). For a V1 board, change it to `qio_qspi`. To check a board, run `pio pkg exec -p tool-esptool -- esptool --port /dev/cu.usbmodem* flash-id`: V2 reports 8 MB embedded PSRAM.

## Preview screens without the board

The screen code in `lib/screen/` also runs on your computer. The simulator draws each screen and saves it as a PNG, so you can check layouts and pixel art without flashing.

```sh
pio run -e native -t exec    # draw every screen into sim_out/
open sim_out                 # look at them in Finder or Preview
```

Images are 3x size (600x600 for the 200x200 panel), with gray "paper" and near-black "ink" to resemble e-ink. Rerun the command after each change; it takes a few seconds.

The simulator also plays a scripted ride through the real UI code (start, page changes, a climb and a stop, pause, finish) and saves the frames that matter to `sim_out/session/`, with how many refreshes it took.

To preview a new screen or state, add a drawing function to `lib/screen/` and call it from `main()` in `sim/main.cpp`, followed by `save(canvas, "name")`.

The PNGs show layout only. Refresh speed, flashing and ghosting need the real panel; see "Refresh budget" in [SPEC.md](SPEC.md).

## Unit tests

Ride logic in `lib/ride/` (GPS parsing, ride stats, GPX) is tested on your computer:

```sh
pio test -e native
```

## Status

Early. The firmware runs the full UI on the board with a fake ride standing in for the GPS. See "Current status" in [SPEC.md](SPEC.md).
