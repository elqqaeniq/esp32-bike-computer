# ESP32 Bike Computer

[![Build](https://github.com/USERNAME/esp32-bike-computer/actions/workflows/build.yml/badge.svg)](https://github.com/USERNAME/esp32-bike-computer/actions/workflows/build.yml)
[![Latest Release](https://img.shields.io/github/v/release/USERNAME/esp32-bike-computer?color=brightgreen&include_prereleases)](https://github.com/USERNAME/esp32-bike-computer/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-brightgreen.svg)](LICENSE)

ESP32-S3 round-screen bike computer with GPS, IMU brake/pothole detection,
BLE heart-rate monitor pairing, WS2812 turn signals, and OTA firmware updates
via captive portal.

> **Status:** v1.1.3 — first successful flash, field tests pending.
> See [CHANGELOG.md](CHANGELOG.md) for version history and
> [PROJECT_NOTES.md](PROJECT_NOTES.md) for roadmap and design rationale.

---

## Features

- **360×360 round QSPI display** (ST77916) with 3 main screens (Riding / Map / Terrain) + 10-screen settings menu
- **GPS** (ATGM336H, 9600 baud, NMEA) — speed, distance, altitude, slope
- **IMU** (MPU6500, I²C) — brake severity, pothole detection, road surface analysis
- **BLE Central** — Garmin Forerunner heart-rate monitoring
- **Front WS2812 turn signals** — 16 LEDs, 5 modes (left/right/hazard/thank-you/DRL)
- **OTA via captive portal** — connect to AP, upload `.bin` from browser
- **Persistent storage** — odometer, calibration, settings in NVS
- **LittleFS partition** reserved (~9.875 MB) for upcoming GPX track export

---

## Hardware

- **MCU:** ESP32-S3-N16R8 (16 MB flash, 8 MB OPI PSRAM)
- **Display:** ST77916 360×360 round, QSPI 4-line bus
- **GPS:** ATGM336H (UART)
- **IMU:** MPU6500 (I²C, 0x68)
- **LEDs:** WS2812B × 16 (front)
- **Power:** 2× 18650 (3000 mAh, 2P)
- **Inputs:** EC11 encoder + 3× TTP223 touch buttons

Full pinout: see [`wiring_diagram_v3.html`](wiring_diagram_v3.html) (open in browser).

---

## Build & Flash

Detailed instructions: [`BUILD.md`](BUILD.md)

Quick path with Arduino IDE:
1. Install ESP32 core 3.x via Boards Manager
2. Install libraries (see [`BUILD.md`](BUILD.md) §2)
3. Open `esp32_bike_computer/esp32_bike_computer.ino`
4. Tools → Board → ESP32S3 Dev Module
5. Tools → Partition Scheme → **Custom** (uses `partitions.csv`)
6. Tools → PSRAM → **OPI PSRAM**
7. Upload

Pre-built firmware: see [Releases](../../releases/latest).
Manual flash with `esptool`: see [`BUILD.md`](BUILD.md) §4.1.

---

## CI & Releases

Every push to `main` triggers a build and produces a downloadable artifact
(retained 30 days, available under the Actions tab).

Tagged commits (`v1.1.3`, `v1.2.0`, etc.) trigger an automated release with:
- Compiled `firmware-X.Y.Z.bin` (for OTA upload)
- `partitions-X.Y.Z.bin` and `bootloader-X.Y.Z.bin` (for full-flash recovery)
- Release notes auto-extracted from `CHANGELOG.md`

> When flashing pre-built binaries manually with `esptool`, always use
> `--flash-mode keep`. See [`BUILD.md`](BUILD.md) §4.1 for details.

Tags matching `v*-beta.N` or `v*-rc.N` are marked as pre-releases.

---

## Project Structure

```
esp32-bike-computer/
├── esp32_bike_computer/         Arduino sketch folder
│   ├── esp32_bike_computer.ino  Main sketch — setup() / loop()
│   ├── partitions.csv           Custom 16 MB partition table
│   ├── config.h                 All pinout, constants, version
│   ├── ble_manager.h            BLE Central (Garmin HR)
│   ├── error_handler.h          Module error tracking
│   ├── led_controller.h         WS2812 animations
│   ├── mpu6500.h                IMU driver
│   ├── ota_update.h             Captive-portal OTA
│   ├── settings.h               10-screen settings UI
│   └── ui_theme.h               Display theme / colors
├── BUILD.md                     Build instructions, troubleshooting
├── CHANGELOG.md                 Version history
├── PROJECT_NOTES.md             Roadmap, calibration values, design rationale
├── wiring_diagram_v3.html       Interactive wiring diagram
└── .github/workflows/build.yml  CI pipeline
```

---

## Status & Roadmap

Current: **v1.1.3** (first successful flash, Dusk2Dawn replaced, field tests pending).

Upcoming:
- **v1.2** — field tests, calibration, GPX track export via web UI
- **v2.0** — NRF52840 rear unit activation (24 LEDs, ANT+ cadence)
- **v3.0** — `.fit` activity files for Garmin Connect / Strava import

Detailed roadmap: [`PROJECT_NOTES.md`](PROJECT_NOTES.md).

---

## License

MIT — see [LICENSE](LICENSE).
