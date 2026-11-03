# Coaster: working notes for agents

Read `SPEC.md` before doing any work. It defines what the device does and what is out of scope for v1.

## Build and flash

- Build: `pio run`
- Flash and monitor: `pio run -t upload && pio device monitor`
- Board: Waveshare ESP32-S3-ePaper-1.54. Check the board version (V1 or V2) on the silkscreen before changing PSRAM settings in `platformio.ini`. The versions have different memory and are not interchangeable: V1 uses quad PSRAM (`board_build.arduino.memory_type = qio_qspi`), V2 uses octal PSRAM (`qio_opi`). Source: `CONFIG_SPIRAM_MODE_*` in the vendor's `02_Example/ESP-IDF/V1` and `V2` sdkconfigs.

## Where things live

- `src/`: project firmware. This is where most changes go.
- `lib/epaper_driver/`, `lib/board_power/`: driver code copied from Waveshare's examples. Don't edit unless asked.
- `vendor/`: full Waveshare repo, git-ignored. Use it as the reference for pins and examples.

## Rules

- Pins come from the vendor's `user_config.h` and examples. Don't guess pin numbers.
- If a pin or setting is unknown, say so and ask. Don't fill it in.
- This is a battery device. Avoid dynamic allocation in the main loop and keep the screen refresh rate low.
- Use partial refresh for small updates and a full refresh only occasionally, to clear ghosting.
- Don't add features listed as out of scope in `SPEC.md` (navigation, app, Bluetooth sensors, weather).
- Keep the device's on-screen text short. One main number at a time.

## Before finishing a task

- Build the firmware and confirm it compiles.
- Note any pin, memory, or power assumptions in the summary.
- Update the "Current status" section of `SPEC.md` if the state of the project changed.
- Update "Open questions" in `SPEC.md`: remove answered ones, add new ones.