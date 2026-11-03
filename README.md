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

Set PSRAM for your board version in `platformio.ini` before flashing. Check the silkscreen: V1 uses `qio_qspi`, V2 uses `qio_opi`.

## Status

Early. Hello-world firmware builds. See "Current status" in [SPEC.md](SPEC.md).
