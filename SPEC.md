# Coaster

A tiny e-ink cycling companion. It records rides to a GPX file, uploads them to Strava when the device reaches home Wi-Fi, and shows a rider pedaling on screen. Built for people who want to ride and log it without a phone, a $300+ computer, or any app.

## Thesis

A tiny companion for the rides you take alone. Records the ride, nothing more.

## Target rider

Casual riders doing coffee rides and social group rides who have a Strava account, but who don't care about performance metrics, training, or sensors.

## Hardware

This is proof-of-concept hardware, chosen to test the idea, not to ship. v1 will likely use a different board and display (see Open questions). Keep everything except the display driver, board power code and pin numbers hardware-independent, and don't invest in tuning specific to this panel, such as custom e-ink waveforms.

### What the proof of concept should answer

- Is one update per second enough for the rider to feel alive?
- Is 1.54" readable at a glance while riding?
- How long does the battery last with GPS running?
- What mount and size feel like "an accessory, not a gadget"?

### Proof-of-concept parts

- Waveshare ESP32-S3-ePaper-1.54 (board version: TBD, V1 or V2; check silkscreen). V1 uses quad PSRAM, V2 uses octal PSRAM. V2 replaced V1 from 2025-11-01.
- 1.54" e-paper, 200x200, black and white, SPI
- Onboard, not yet used: PCF85063 RTC (useful for timestamps before GPS fix and for wake from sleep), SHTC3 temperature/humidity sensor, audio codec
- GPS module: Teyleten / NEO-6M class, UART (pins: TBD)
- Barometer: BMP280 class, I2C (shared bus, see Pinout)
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
- A pixel rider on screen, seen from the side. A silly character (an antenna blob), not a realistic cyclist:
  - Climbing: pedals hard, leaning forward
  - Fast: flattened by the wind, speed lines
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
- Screen drawing lives in `lib/screen/` and can be previewed as PNGs on a Mac (env `native`). A placeholder speed screen exists, with the rider optional: with it, the number shrinks to the top and the rider sits at the bottom. It isn't shown by the main firmware yet.
- Panel glue (power, init, full and partial refresh) lives in `lib/display/`, shared by the firmware and the refresh test. The vendor driver always sends the whole frame; a "partial" refresh uses the partial waveform, so only changed pixels move.
- Refresh test firmware (`hwtest/refresh_test.cpp`, env `refresh-test`): a fake ride at one partial refresh per second, with the rider changing pose, and a full refresh every N partials. Prints refresh time and battery voltage as CSV over serial; serial commands change N and turn the rider on or off. Builds; not yet run on the board.
- Side-view pixel rider (`lib/screen/rider.cpp`): a silly antenna blob on a bike. Three poses: stopped (dozing, one foot down, snot bubble, Zz), fast (squashed flat, antenna blown back, speed lines) and climbing (on a slope, tongue out, sweating). These are drafts; the final poses will be hand-drawn and dropped in `art/rider/`. Each pose is 48x48 pixels drawn at 2x, filling a 96x96 box, so a pose change is one partial refresh of that box. Not yet on the speed screen or the firmware.

## Next steps

1. Flash hello-world. Set PSRAM mode for the board version first (V1: `qio_qspi`, V2: `qio_opi`).
2. Run the refresh test: refresh times, partials before visible ghosting, whether 1 Hz feels alive, readability at a glance.
3. Deep sleep between screen updates.
4. Hand-drawn rider poses; decide the screen layout.
5. GPS over UART, then log GPX to flash.
6. Barometer over I2C for elevation.
7. Sync and setup flow.

## Open questions

- Board version: V1 or V2? Determines PSRAM mode.
- Start/stop button: BOOT (GPIO 0) or PWR (GPIO 18)?
- How to switch between screens (speed, distance, time, elevation)? Second button, long press, or auto-rotate?
- GPS UART pins: which free GPIOs?
- Power while recording: 1 Hz logging needs the chip awake or in light sleep. Deep sleep only when idle? Switch GPS power off when not recording?
- GPX storage: flash or microSD? How to recover a file left unclosed by power loss?
- Strava OAuth: the phone has no internet while joined to the device's AP. Do OAuth after the device joins home Wi-Fi, served at its local IP? Where does the client secret live?
- Thesis says "rides you take alone"; target rider includes social group rides. Which is it?
- Rider poses have one frame each. Does the fast rider need a second frame (pedal in another spot) to feel alive at 1 Hz, and is that worth the extra partial refresh?
- No pose for ordinary riding on the flat (cruising), which is most of a ride. Add one?
- UI layout: one screen or several pages? Is the rider always shown, or can it be turned off? The placeholder puts the rider under the number; the number shrinks from 70 to 56 px tall to fit.
- Battery voltage reads GPIO 4 (ADC1 channel 3, from the vendor ADC example) through a 1:2 divider. Confirm against the schematic.
- Strava API app approval needed before sharing beyond personal use.
- v1 hardware: which board and display? E-ink holds the image with no power and looks like paper, but limits animation to about one update per second. Memory-in-pixel LCDs (Sharp Memory LCD, JDI, as used in many sports watches and bike computers) are also sunlight-readable and low-power, and refresh fast enough for smooth animation with no ghosting, but they need some power to hold the image. Decide after the proof of concept.
