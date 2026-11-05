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

- Waveshare ESP32-S3-ePaper-1.54 V2: ESP32-S3-PICO-1 with 8 MB flash and 8 MB octal PSRAM, confirmed with `esptool flash-id`. (V1 used quad PSRAM; V2 replaced it from 2025-11-01.)
- 1.54" e-paper, 200x200, black and white, SPI
- Onboard, not yet used: PCF85063 RTC (useful for timestamps before GPS fix and for wake from sleep), SHTC3 temperature/humidity sensor, microphone
- Audio: ES8311 codec and NS4150B speaker amplifier onboard, speaker in the 2-pin GH1.25 socket. Used for the ride start sound.
- GPS module: ATGM336H (AT6558 chip, GPS + BeiDou), UART, NMEA 4.1 with `GN` talker. Baud assumed 9600, to confirm. Pins: TBD (not wired yet)
- Barometer: BMP280 class, I2C (shared bus, see Pinout)
- LiPo battery via the board's MX1.25 header
- microSD via the onboard TF slot (FAT32)

## Pinout

Source of truth is `vendor/ESP32-S3-ePaper-1.54/` and `lib/board_power/`. Do not guess pins.

From `vendor/ESP32-S3-ePaper-1.54/02_Example/Arduino/*/user_config.h`, `04_SD_Card/sdcard_bsp.cpp` and `08_Audio_Test/src/codec_board/board_cfg.txt`:

| Function | GPIO |
|---|---|
| E-paper DC / CS / SCK / MOSI | 10 / 11 / 12 / 13 |
| E-paper RST / BUSY | 9 / 8 |
| E-paper power | 6 |
| Audio power | 42 |
| Battery power hold | 17 |
| Battery voltage (ADC1 ch 3, 1:2 divider) | 4 |
| I2C SDA / SCL | 47 / 48 |
| microSD (SDMMC 1-bit) CLK / CMD / D0 | 39 / 41 / 40 |
| Audio I2S MCLK / BCLK / WS / DOUT / DIN | 14 / 15 / 38 / 45 / 16 |
| Speaker amplifier enable (high = on) | 46 |
| BOOT button (also deep-sleep wake pin in vendor example) | 0 |
| PWR button | 18 |
| GPS UART TX / RX | TBD: pick free GPIOs, confirm against schematic |

Header J1 (12 pins) is labeled RXD, TXD, SDA, SCL, GP1, GP2, GP3, GP19, GP20, 3V3, VSYS and, per the schematic, GND. SDA/SCL are GPIO 47/48, with 4.7 kΩ pull-ups on the board. RXD/TXD are UART0 (GPIO 44/43, boot messages), GP19/GP20 are the USB data lines (don't use), GP3 drives the green LED, VSYS is battery/USB voltage (not for 3.3 V sensors). Source: the only schematic in the vendor repo, for the Touch variant, which matches the labels on our V2 board. I2C addresses on the shared bus: PCF85063 RTC 0x51, SHTC3 0x70, ES8311 0x18, BMP280 0x76 or 0x77 (no conflict).

## Screen

- One main number per page. Fewer pages, bigger type. No dense stats. The dashboard and elevation pages are the exceptions: two small numbers in the top corners.
- Units: mph, miles, feet for now (`UI_IMPERIAL` in `lib/ui/ui.h`). Recording and GPX stay metric.
- Three pages, in this order:
  1. Rider: one number with the rider under it. Speed while riding; distance when paused or finished (speed would be 0).
  2. Dashboard, no rider: speed in the middle, distance top-left, elapsed time (h:mm) top-right. On the summary the middle number is average speed.
  3. Elevation, no rider: feet climbed in the middle, current elevation top-left, battery % top-right. Current elevation changes on climbs, so this page refreshes up to once a second there.
- Average speed appears only on the summary. Max speed isn't shown; it's in the GPX and on Strava.
- A pixel rider on screen, seen from the side. A silly character (an antenna blob), not a realistic cyclist. Rest states are never failure states. Poses:
  - Asleep (board in deep sleep): asleep in bed. E-paper keeps the image with no power, so a sleeping device shows him sleeping.
  - Board wakes up: sits up in bed, "good morning"
  - Idle, ready to ride: drinking a coffee at a table
  - Cruising: riding on the flat
  - Climbing: pedals hard, leaning forward
  - Fast: flattened by the wind, speed lines
  - Stopped mid-ride: dozing on the bike
  - Paused by hand: drinking a coffee next to the bike
  - Ride finished: no rider for now, just the total distance
- Use partial refresh for small updates; full refresh periodically to clear ghosting.

### Buttons and states

Two buttons: BOOT (GPIO 0) only changes pages; PWR (GPIO 18) only controls the ride, and also powers the board on. Separate jobs avoid the delay and misfires of single/double-press detection on one button. Button presses map to actions in `lib/ui`, so a one-button v1 would only need a different mapping.

| State | Screen | BOOT | PWR press | PWR long press (1 s) |
|---|---|---|---|---|
| Waking (3 s, at power-on and wake) | sits up in bed, "good morning" | | | |
| Idle | coffee, "hold PWR to ride", battery % | | | start ride |
| Sleeping (after 2 min idle) | asleep in bed, "press PWR" (no battery: it would go stale); board in deep sleep | wake | wake | wake |
| Riding | pages (live rider on page 1) | next page | pause | pause |
| Paused | pages (coffee on page 1) | next page | resume | finish ride |
| Finished | summary pages (distance on page 1, no rider) | next page | | back to idle |

Finishing always goes through a pause, so a single slip can't end a ride. The summary opens on the total distance. The PWR press that powers the board on is ignored until released.

Full refreshes go where the screen changes anyway: waking up, starting a ride, pausing, finishing, falling asleep, and stopping mid-ride (the rider dozes off; this one happens on any page). A fallback full refresh comes after 40 partials.
- Weather is out of scope for v1.

### Refresh budget

E-paper is not a video display. These limits shape the screen and the rider design.

- While riding: at most one partial refresh per second, in step with the 1 Hz GPS fix. Skip the refresh when nothing visible changed (for example, the rounded speed is the same).
- Speed, rider and any other changes go out together in one partial refresh, never separately.
- Full refresh (about 2 s, flashes black and white) clears ghosting. Do it when the rider stops; the "falling asleep" moment hides the flash. On long rides without stops, also do one periodically.
- Waveshare's guidance is vague: one FAQ says to do a full refresh after 5 partials, and warns that skipping full refreshes worsens ghosting and can damage the panel. Other Waveshare manuals say "after several". First run on the board (V2): little visible ghosting with a full refresh every 30 partials at 1 Hz, and probably more is fine. Not yet tested over a long ride or at temperature extremes, and the long-term panel risk Waveshare warns about is unknown.
- The rider is a set of poses, not smooth animation: 2 frames per pose that differ in as few pixels as possible. The panel's 611 ms partial refresh caps animation at about 1 fps; anything faster needs a different display (see v1 hardware). Keep the sprite compact; large black areas that move ghost the most.
- Animation rates (`lib/ui/ui.cpp`): cruising and fast step every GPS fix (1 fps), climbing and dozing at a stop every 2 fixes. Riding frames step with the fixes, so rider and numbers go out in one refresh. Rest poses (bed, waking, coffee) are single-frame stills, so idle and paused screens only refresh when a page or value changes. The dashboard and elevation pages have no rider and don't animate. On the rider page the moving poses (cruising, fast, climbing) also ride right to left, 16 px per frame (climbing diagonally, up the slope), off the left edge and back in from the right, the way the art faces. Other poses sit centered, and riding resumes from the middle after a stop.
- Measured on the V2 board: a partial refresh takes 611 ms (whole frame through the vendor driver). That leaves about 0.4 s of each 1 Hz second with a still screen, and is why updates look clunky. Full refresh: 2150 ms in the main firmware. Still to measure: the upper limit on partials between full refreshes (40 tested so far), energy per refresh.
- Screens can be previewed on a Mac without the board: `pio run -e native -t exec` writes PNGs to `sim_out/`.

## Recording

- Starts with a long press of PWR; pausing and finishing are described under "Buttons and states".
- A pause by hand starts a new GPX track segment, and the first fix after resuming adds no distance or climbing, so a train ride during a pause doesn't count.
- Logs GPX to the onboard flash, one trackpoint per second. microSD only if flash turns out too small or too slow.
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

- Firmware (`src/main.cpp`, env `waveshare-epaper-154`) runs the full UI on the V2 board: both buttons, all states and pages, rider poses and the refresh policy. Fixes come from the GPS over UART (`Serial1`, 4 KB receive buffer so a 2 s full refresh loses nothing; fixes without a position are ignored). The GPS pins are not set yet, so the GPS builds stop with an `#error` until they are, and `pio run` builds env `fake-ride` (the same firmware with `lib/fake_ride/` supplying one fix per second) until then. GPS test firmware `hwtest/gps_test.cpp` (env `gps-test`) echoes raw NMEA and prints parsed fixes; written, waiting on wiring. Each ride is saved as a GPX file (see "Ride storage" below). Buttons are polled in their own FreeRTOS task, so presses during a refresh aren't lost. Flashed; not yet tried by hand on the board. The board connects over USB as `/dev/cu.usbmodem*`; octal PSRAM confirmed working.
- UI logic (`lib/ui/`): debounced buttons with short and long press, the state machine, the three pages and their values in mph/mi/ft, rider pose choice (fast above 13 mph, climbing by grade over the last 30 m or more, held 3 s before changing, 5 s before dozing off) and the refresh decision (skip when nothing visible changed). Hardware-independent and unit-tested.
- Screen drawing lives in `lib/screen/` and can be previewed as PNGs on a Mac (env `native`): pages (number auto-sized to fit, unit, optional rider), the dashboard (big number plus two corner numbers) and scenes (the rider at 3x with a caption, battery % in the corner on idle). The simulator also plays a scripted session through the real UI code into `sim_out/session/`, until the rider falls asleep after the ride: 481 s with 10 full refreshes, 149 partials and 322 skipped (127 partials in 282 s of riding, with time on pages that don't animate).
- Panel glue (power, init, full and partial refresh) lives in `lib/display/`, shared by the firmware and the refresh test. The vendor driver always sends the whole frame; a "partial" refresh uses the partial waveform, so only changed pixels move.
- Ride storage (`lib/ride_log/`, unit-tested on a Mac): one GPX file per ride on a LittleFS partition (`partitions.csv`: one 2 MB app slot, no over-the-air updates, 5.9 MB for rides, about 16 hours). The file opens at the first fix with a position (no fix all ride: no file), named from its UTC time (`20261103-071542`). It is `.part` while recording, synced to flash every 10 points and on pause (a power cut loses at most 10 s); a pause starts a new track segment; finishing adds the footer and renames it to `.gpx`. At boot, any `.part` is repaired (complete lines kept, footer added) and the stored rides are listed over serial. The firmware formats the partition only if it is blank; a mount failure on a non-blank partition is reported and nothing is formatted. Plain C stdio, so the tests run against a temp directory. On the board: a fake ride with a pause saved 100 points (99 s); the power-cut repair is tested only on the Mac so far. The fake ride always starts at the same time, so fake rides overwrite each other's file.
- Ride recording logic (`lib/ride/`), hardware-independent and unit-tested on a Mac (`pio test -e native`): an NMEA parser (RMC + GGA, any talker, NMEA 4.1 extra fields fine, tested on ATGM336H-style sentences), ride stats (distance, elapsed and moving time, average and max speed, elevation gain) and a GPX 1.1 writer that formats one line per trackpoint into a caller buffer. No allocation. Fed from the GPS UART by the main firmware. Tuning: below 3 km/h counts as stopped (no distance, no moving time); speeds over 120 km/h are ignored as glitches; elevation is smoothed (about 10 s) with a 3 m threshold.
- Barometer driver (`lib/baro/`): BMP280 or BME280 over I2C, forced mode (one reading on demand, sleeps in between), x16 pressure oversampling, IIR filter 4. Compensation math is from the Bosch datasheet and unit-tested against its worked example; pressure to altitude is in `lib/ride`. Test firmware `hwtest/baro_test.cpp` (env `baro-test`) scans the I2C bus and prints altitude relative to the first reading. Builds; not yet run with the sensor wired.
- Refresh test firmware (`hwtest/refresh_test.cpp`, env `refresh-test`): a fake ride at one partial refresh per second, with the rider changing pose, and a full refresh every N partials. The rider alternates its two frames every tick; animation-only mode (`a`) holds speed and pose so only the frames change, the worst case for ghosting. Prints refresh time, partials since the last full refresh and battery voltage as CSV over serial; serial commands change N and turn the rider on or off. Ran on the board: speed and rider poses update as expected. Little ghosting at a full refresh every 30 to 40 partials. A partial refresh takes 611 ms, which looks clunky.
- Sound (`lib/sound/`): one generated tone, no sound files: a single note rising from E6 to A6 (180 ms) when a ride starts. Nothing else beeps; sounds on every button were too much, and distracting on a group ride. The synth is hardware-independent and unit-tested; `sound_es8311.cpp` sets up the ES8311 over I2C (register sequence from the vendor's esp_codec_dev driver) and streams 16 kHz audio over I2S. Test firmware `hwtest/sound_test.cpp` (env `sound-test`): BOOT and PWR play the sound, serial commands change volume and switch to powering the codec only around each sound. Ran on the board: the codec answers on I2C and powers up in 19 ms; sounds are clear and can get loud. The main firmware plays it from a sound task, alongside the full refresh to the waking rider. The codec and amplifier are powered only around each sound, and the audio rail is switched off at boot. Volume is `SOUND_VOLUME` in `src/main.cpp` (0 dB).
- Power (`src/main.cpp`): battery voltage on GPIO 4 (x2 divider), averaged over 8 reads every minute, converted to percent by `lib/battery` (unit-tested) and logged over serial. After 2 minutes idle the UI shows the sleep screen (full refresh) and the board goes into deep sleep, using the vendor's sequence from `11_RTC_Sleep_Test`: GPIO 17 held high so it stays on with the battery alone, wake when BOOT or PWR pulls low. Waking restarts the firmware into idle. In deep sleep the USB serial port disappears; press a button before flashing. Ran on the board: sleeps after 2 minutes idle and wakes on a button press. Current while asleep not yet measured.
- Side-view pixel rider (`lib/screen/rider.cpp`): a silly antenna blob, seven poses of 48x48 pixels, drawn at 2x on pages and 3x in scenes. All are commissioned hand-drawn art in `art/rider/` (`name_1.png`/`name_2.png` for two frames, `name.png` for one), converted to `lib/screen/rider_art.h` by `python3 scripts/rider_art.py`, which rejects any size other than 48x48 and any colors except black and transparent. Two frames: stopped (Zz drift), fast (speed lines shift), climbing (sweat drops), cruising (pedals). One frame, by design: bed (asleep, the sleep screen), waking ("!", when the board wakes), coffee (at a table: idle, and paused on the rider page). The finished summary has no rider for now. Flashed to the board in the refresh test (stopped, climbing, cruising and fast at 2x): runs, 611 ms partials as before.

## Next steps

1. Ride storage on the board: a power cut mid-ride and the repair at boot. Then a way to get a GPX off the device for checking (serial dump?) until sync exists.
2. GPS: wire the ATGM336H (GP1/GP2 on J1?), set the pins in `src/main.cpp` and `hwtest/gps_test.cpp`, run the GPS test (baud, fix time indoors and out), then set `default_envs` back to `waveshare-epaper-154`.
3. A first real ride: readability at arm's length, whether 1 fps pedaling feels alive, button feel with gloves, battery life with GPS on.
4. Power: measure the current drawn in deep sleep; light sleep between updates while riding. Steadier battery % (read when quiet, average over minutes, 5% steps).
5. Animation ghosting: run the refresh test in animation-only mode (`a`) and find how many partials the rider's two frames can alternate before ghosting shows. Set `UI_FULL_EVERY` from that, and find the upper limit on partials between full refreshes.
6. Sound: pick a volume; listen for a pop when the amplifier switches on.
7. Barometer over I2C for elevation.
8. Sync and setup flow.

## Open questions

- A partial refresh takes 611 ms and looks clunky. Panel-specific waveform tuning is out of scope. Options: skip refreshes when nothing visible changed (a steady speed changes rarely on a real ride), and update less than once a second when only the rider would move. Does it matter on the v1 display? A memory LCD wouldn't have this problem.
- GPS UART pins: which free GPIOs? Candidates: GP1 and GP2 on header J1, the only free GPIOs there.
- Power while recording: 1 Hz logging needs the chip awake or in light sleep. Deep sleep only when idle? Switch GPS power off when not recording? On J1's 3V3 the GPS stays powered while the board is in deep sleep (the hold keeps the rail up), so it drains the battery while asleep; it needs a switched supply or its own enable pin.
- Ride storage: 5.9 MB holds about 16 hours. What happens when it's full: stop saving (now), or delete the oldest uploaded ride? Rides are deleted only after upload, once sync exists. Show a storage or save error on screen, not only over serial? No over-the-air updates with this partition table; revisit if OTA is wanted (two app slots of 2 MB would still leave about 3.9 MB).
- Elevation gain uses GPS altitude until the barometer is in, and GPS altitude wanders by several meters. Strava recalculates elevation for GPX from devices without a barometer, so the on-device number may differ from Strava's. Is that acceptable for v1?
- Stop threshold of 3 km/h: right for slow climbs and walking the bike? Check against a real ride.
- Strava OAuth: the phone has no internet while joined to the device's AP. Do OAuth after the device joins home Wi-Fi, served at its local IP? Where does the client secret live?
- Thesis says "rides you take alone"; target rider includes social group rides. Which is it?
- Animation cost: at 1 fps on the rider page, the fallback full refresh (every 40 partials) flashes about every 40 s while riding. How many partials can alternating frames take before ghosting shows? How much battery does animation cost compared with a still rider? The rider now also travels across the page, so each frame moves its whole outline instead of a few pixels: does that ghost more, and does it need full refreshes more often than every 40 partials?
- Sound: audible over wind and traffic from a small speaker? How much does it draw while off? The vendor's `board_power_bsp_t` (used by `lib/display/`) sets GPIO 42 as an output, probably leaving the audio rail on, so the firmware switches it off after display setup. Should there be a way to mute it?
- Dashboard time: elapsed time (includes stops and pauses) or moving time? It shows elapsed; the summary's average speed uses moving time, like Strava.
- Sleep vs. power off: after 2 minutes idle the board goes into deep sleep (wakes on a button, a few tens of µA for the chip). Cutting the battery hold (GPIO 17) instead would draw nothing, but needs PWR to power back on and does nothing on USB. Is deep sleep low enough? Measure it.
- Waking from sleep: the PWR press that wakes the board is ignored until released, so starting a ride from sleep is a press, then a long press. Let the waking press count toward the long press?
- Battery %: a generic LiPo curve (`lib/battery`), read every minute. It reads near 100% on USB (charging) and drops under load. Calibrate with a discharge test. Show it while riding? Warn when low?
- Waiting for GPS: starting a ride before the GPS has a fix. Wait with a "looking for satellites" screen, or start and fill in once it has one?
- Finished screen: go back to idle by itself after a while, or only on a long press?
- Units: imperial is a compile-time switch for now. Make it a setting in the Wi-Fi setup page?
- Strava API app approval needed before sharing beyond personal use.
- v1 hardware: which board and display? E-ink holds the image with no power and looks like paper, but limits animation to about one update per second. Memory-in-pixel LCDs (Sharp Memory LCD, JDI, as used in many sports watches and bike computers) are also sunlight-readable and low-power, and refresh fast enough for smooth animation with no ghosting, but they need some power to hold the image. Decide after the proof of concept.
