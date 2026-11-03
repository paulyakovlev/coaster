# Coaster

A tiny e-ink cycling companion. It records rides to a GPX file, uploads them to Strava when the device reaches home Wi-Fi, and shows a rider pedaling on screen. Built for people who want to ride and log it without a phone, a $300+ computer, or any app.

## Thesis

A tiny companion for the rides you take alone. Records the ride, nothing more.

## Target rider

Casual riders doing coffee rides and social group rides who have a Strava account, but who don't care about performance metrics, training, or sensors.

## Hardware

- Waveshare ESP32-S3-ePaper-1.54 (board version: TBD, V1 or V2; check silkscreen). V1 uses quad PSRAM, V2 uses octal PSRAM. V2 replaced V1 from 2025-11-01.
- 1.54" e-paper, 200x200, black and white, SPI
- Onboard, not yet used: PCF85063 RTC (useful for timestamps before GPS fix and for wake from sleep), SHTC3 temperature/humidity sensor, audio codec
- GPS module: Teyleten / NEO-6M class, UART (pins: TBD)
- Barometer: BMP280 class, I2C (pins: TBD)
- LiPo battery via the board's MX1.25 header
- microSD via the onboard TF slot (FAT32)

## Pinout

Source of truth is `vendor/ESP32-S3-ePaper-1.54/` and `lib/board_power/`. Do not guess pins.

From `vendor/ESP32-S3-ePaper-1.54/02_Example/Arduino/*/user_config.h` and `04_SD_Card/sdcard_bsp.cpp`:

| Function | GPIO |
|---|---|
| E-paper DC / CS / SCK / MOSI | 10 / 11 / 12 / 13 |
| E-paper RST / BUSY | 9 / 8 |
| E-paper power | 6 |
| Audio power | 42 |
| Battery power hold | 17 |
| I2C SDA / SCL | 47 / 48 |
| microSD (SDMMC 1-bit) CLK / CMD / D0 | 39 / 41 / 40 |
| BOOT button (also deep-sleep wake pin in vendor example) | 0 |
| PWR button | 18 |
| GPS UART TX / RX | TBD: pick free GPIOs, confirm against schematic |

I2C addresses on the shared bus: PCF85063 RTC 0x51, SHTC3 0x70, BMP280 0x76 or 0x77 (no conflict).

## Screen

- Shows one main number at a time. Fewer pages, bigger type. No dense stats.
- Shows speed, distance, elapsed time, and elevation.
- A pixel rider on screen:
  - Climbing: pedals hard, leaning forward
  - Fast: aero tuck, speed lines
  - Stopped: sleeping, snoring, or setting up camp. A rest state, never a failure state.
- Use partial refresh for small updates; full refresh periodically to clear ghosting.
- Weather is out of scope for v1.

### Refresh budget

E-paper is not a video display. These limits shape the screen and the rider design.

- While riding: at most one partial refresh per second, in step with the 1 Hz GPS fix. Skip the refresh when nothing visible changed (for example, the rounded speed is the same).
- Speed, rider and any other changes go out together in one partial refresh, never separately.
- Full refresh (about 2 s, flashes black and white) clears ghosting. Do it when the rider stops; the "falling asleep" moment hides the flash. On long rides without stops, also do one periodically.
- Waveshare's guidance is vague: one FAQ says to do a full refresh after 5 partials, and warns that skipping full refreshes worsens ghosting and can damage the panel. Other Waveshare manuals say "after several". The real interval is to be measured on the board.
- The rider is a set of poses, not smooth animation: 1 to 2 frames per state, changing at most once per second. Keep the sprite compact; large black areas that move ghost the most.
- Measure on the board: partial and full refresh time, partials before visible ghosting at 1 Hz, energy per refresh.
- Screens can be previewed on a Mac without the board: `pio run -e native -t exec` writes PNGs to `sim_out/`.

## Recording

- Starts and stops with one button press.
- Logs GPX to flash or microSD, one trackpoint per second.
- Works fully offline.
- Keeps elapsed time, distance, current/average/max speed, and elevation gain.

## Sync

- When the device reconnects to home Wi-Fi, it uploads queued GPX files to Strava.
- Queue holds multiple rides. Uploads resume after a dropped connection.
- Setup: device broadcasts its own Wi-Fi access point, shows a QR code, and serves a captive portal for home Wi-Fi and Strava OAuth. No app, no Bluetooth.

## Out of scope for v1

- Turn-by-turn navigation (a "follow this GPX" mode may come later)
- Companion app
- Bluetooth sensors, power, heart rate, cadence
- Phone GPS proxy
- Weather
- Watch integration

## Design principles

- Records, doesn't coach. Never scolds for a missed week.
- No app to install. Setup happens through a QR code.
- Should look like an accessory, not a gadget.
- Enclosure and character shells are a later direction; keep electronics generic.

## Current status

- Hello-world firmware exists in `src/main.cpp`. Builds cleanly with PlatformIO 6.2 (env `waveshare-epaper-154`). Not yet flashed; waiting for the board.
- Screen drawing lives in `lib/screen/` and can be previewed as PNGs on a Mac (env `native`). A placeholder speed screen exists; it isn't shown by the firmware yet.

## Next steps

1. Flash hello-world. Set PSRAM mode for the board version first (V1: `qio_qspi`, V2: `qio_opi`).
2. Deep sleep between screen updates.
3. Rider sprite and the speed screen.
4. GPS over UART, then log GPX to flash.
5. Barometer over I2C for elevation.
6. Sync and setup flow.

## Open questions

- Board version: V1 or V2? Determines PSRAM mode.
- Start/stop button: BOOT (GPIO 0) or PWR (GPIO 18)?
- How to switch between screens (speed, distance, time, elevation)? Second button, long press, or auto-rotate?
- GPS UART pins: which free GPIOs?
- Power while recording: 1 Hz logging needs the chip awake or in light sleep. Deep sleep only when idle? Switch GPS power off when not recording?
- GPX storage: flash or microSD? How to recover a file left unclosed by power loss?
- Strava OAuth: the phone has no internet while joined to the device's AP. Do OAuth after the device joins home Wi-Fi, served at its local IP? Where does the client secret live?
- Thesis says "rides you take alone"; target rider includes social group rides. Which is it?
- Strava API app approval needed before sharing beyond personal use.
