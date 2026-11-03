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

Set PSRAM for your board version in `platformio.ini` before flashing. Check the silkscreen: V1 uses `qio_qspi`, V2 uses `qio_opi`.

## Preview screens without the board

The screen code in `lib/screen/` also runs on your computer. The simulator draws each screen and saves it as a PNG, so you can check layouts and pixel art without flashing.

```sh
pio run -e native -t exec    # draw every screen into sim_out/
open sim_out                 # look at them in Finder or Preview
```

Images are 3x size (600x600 for the 200x200 panel), with gray "paper" and near-black "ink" to resemble e-ink. Rerun the command after each change; it takes a few seconds.

To preview a new screen or state, add a drawing function to `lib/screen/` and call it from `main()` in `sim/main.cpp`, followed by `save(canvas, "name")`.

The PNGs show layout only. Refresh speed, flashing and ghosting need the real panel; see "Refresh budget" in [SPEC.md](SPEC.md).

## Status

Early. Hello-world firmware builds, and screens can be previewed on a computer. See "Current status" in [SPEC.md](SPEC.md).
