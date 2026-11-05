# Coaster: working notes for agents

Read `SPEC.md` before doing any work. It defines what the device does and what is out of scope for v1.

## Build and flash

- Build: `pio run`
- Flash and monitor: `pio run -t upload && pio device monitor`
- Preview screens on this computer: `pio run -e native -t exec` writes PNGs to `sim_out/`. Look at them after any screen change.
- Board: Waveshare ESP32-S3-ePaper-1.54. Check the board version (V1 or V2) on the silkscreen before changing PSRAM settings in `platformio.ini`. The versions have different memory and are not interchangeable: V1 uses quad PSRAM (`board_build.arduino.memory_type = qio_qspi`), V2 uses octal PSRAM (`qio_opi`). Source: `CONFIG_SPIRAM_MODE_*` in the vendor's `02_Example/ESP-IDF/V1` and `V2` sdkconfigs.

## Where things live

- `src/`: project firmware. This is where most changes go.
- `lib/screen/`: all screen drawing, into a `GFXcanvas1`. No hardware calls here, so it also builds for the simulator.
- `sim/`: screen simulator for the `native` env. `sim/stubs/` stands in for the Arduino headers Adafruit GFX needs.
- `lib/epaper_driver/`, `lib/board_power/`: driver code copied from Waveshare's examples. Don't edit unless asked.
- `vendor/`: full Waveshare repo, git-ignored. Use it as the reference for pins and examples.

## Rules

- The Waveshare board is proof-of-concept hardware; v1 will use something else. Keep everything except `lib/epaper_driver/`, `lib/board_power/` and pin numbers hardware-independent. Don't spend effort on tuning specific to this panel, such as custom waveforms.

- Pins come from the vendor's `user_config.h` and examples. Don't guess pin numbers.
- If a pin or setting is unknown, say so and ask. Don't fill it in.
- This is a battery device. Avoid dynamic allocation in the main loop and keep the screen refresh rate low.
- Use partial refresh for small updates and a full refresh only occasionally, to clear ghosting.
- Don't add features listed as out of scope in `SPEC.md` (navigation, app, Bluetooth sensors, weather).
- Keep the device's on-screen text short. One main number per page; the dashboard and elevation pages are the only exceptions (two small corner numbers).

## Keep it simple

This is a proof of concept. Build the smallest thing that answers the question at hand.

- Before writing code, take the first option that works: skip it if nobody asked for it; reuse what's already in `lib/`; use the Arduino/ESP-IDF or C standard library; use an installed library. Only then write new code, and keep it minimal.
- No abstraction, option, config switch or "for later" generality nobody asked for. Write for the one case that exists (one sound, one board, one file format), not a family of them.
- Don't add a library for a few lines of code. Ask before adding any dependency.
- Keep the existing style: plain C-style modules with structs and functions, no classes or layers on top.
- Deleting code beats adding it. Remove code that's left over once its feature is gone.
- A known shortcut gets a one-line comment: `shortcut: <the limit>, <when to upgrade>`.
- Never cut: source comments for pins and register values, error handling that prevents data loss (GPX files), or unit tests for parsing and ride math.

## Before finishing a task

- Build the firmware and confirm it compiles.
- Note any pin, memory, or power assumptions in the summary.
- Update the "Current status" section of `SPEC.md` if the state of the project changed.
- Update "Open questions" in `SPEC.md`: remove answered ones, add new ones.