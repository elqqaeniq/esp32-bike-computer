# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash

**Compile (Arduino IDE):** Open `esp32_bike_computer/esp32_bike_computer.ino`, select board + settings below, then Sketch → Upload.

**Compile (CI / arduino-cli):**
```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=custom,UploadSpeed=921600" \
  esp32_bike_computer/
```

**Board settings (all required):**
- Board: ESP32S3 Dev Module
- PSRAM: **OPI PSRAM** (mandatory — track buffer lives in PSRAM)
- Flash Size: 16MB (128Mb)
- Flash Mode: QIO 80MHz
- Partition Scheme: Custom (uses `partitions.csv`)

**Manual flash (pre-built binaries from CI/Releases):**
```bash
python3 -m esptool --chip esp32s3 --port /dev/ttyACM0 \
  --baud 921600 --before default-reset --after hard-reset \
  write-flash --flash-mode keep --flash-size 16MB \
  0x0000 bootloader-X.Y.Z.bin \
  0x8000 partitions-X.Y.Z.bin \
  0x10000 firmware-X.Y.Z.bin
```
`--flash-mode keep` is critical — the CI bootloader uses DIO mode; using `qio` causes an `ets_loader.c 78` boot failure.

**OTA update:** Settings → OTA Update → Start OTA → connect to "BikeComp_OTA" WiFi → upload `.bin` to `192.168.4.1`.

## Required Libraries

Install these before compiling:

| Library | Source |
|---------|--------|
| GFX Library for Arduino (Arduino_GFX) | Arduino Library Manager |
| TinyGPSPlus | Arduino Library Manager |
| Adafruit NeoPixel | Arduino Library Manager |
| ESPAsyncWebServer | Git: ESP32Async fork (not me-no-dev — broken on Core 3.x) |
| AsyncTCP | Git: ESP32Async fork |

Built-ins used: `Preferences`, `LittleFS`, `BLEDevice`, `WiFi`, `DNSServer`.

## Versioning

For tagged releases, `FW_VERSION` in `config.h` **must match the git tag** exactly — CI enforces this with an error annotation and will fail the build if they diverge.

## Architecture

**Entry point:** `esp32_bike_computer/esp32_bike_computer.ino` (~1150 lines)  
**Modules (header files):** `config.h`, `ui_theme.h`, `error_handler.h`, `ble_manager.h`, `mpu6500.h`, `led_controller.h`, `ota_update.h`, `settings.h`

### Threading Model

The firmware runs two threads:
- **Core 1 (main loop):** ~40ms cycle — GPS parsing, display updates (400ms screens / 1000ms status bar), encoder/button handling, BLE reads, battery ADC, settings
- **Core 0 (imuTask):** FreeRTOS task at 50 Hz — reads MPU6500 accel/gyro, runs brake/pothole/vibration detection

**Thread safety:** IMU data shared between cores via `gIMUSnapshot` + `portMUX_TYPE` spinlock. Main loop calls `refreshIMUSnapshot()` to take a safe copy before reading IMU values. IMU calibration writes are requested via `gCalibRequested` flag (set in main, executed in imuTask).

**OTA safety:** BLE stack is deinited before starting the WiFi AP for OTA, to reclaim ~30–40 KB for the binary upload.

### Key Globals (main sketch)

| Global | Purpose |
|--------|---------|
| `gTrackPoints[]` / `gTrackCount` | PSRAM-allocated track buffer (max 2000 points) |
| `gIMUSnapshot` | Thread-safe copy of IMU data (updated by `refreshIMUSnapshot()`) |
| `gHeartRateBpm` | Written by BLE callback, read by main loop |
| `gErrors` | Error bitmask (10 error codes); written from multiple contexts — web handler race is a known open issue |
| `gMapOffX/Y/Scale` | Map pan/zoom state; declared `volatile` for ISR access |

### Display

Driver: `Arduino_GFX_Library` with ST77916 (360×360 round IPS LCD) over **QSPI** (4-wire parallel, no DC pin — not standard SPI). Backlight uses LEDC PWM on GPIO 21 with the Core 3.x `ledcAttach()` API (not the old `ledcSetup()`/`ledcAttachPin()` API).

### NRF52840 Rear Unit

All rear-unit BLE code (cadence, rear LEDs, ANT+) is commented out and deferred to v2.0. Look for `BLE_NRF_*` defines in `config.h`, `ble_manager.h`, and `led_controller.h`.

## Partition Layout (16 MB)

| Address | Name | Size | Purpose |
|---------|------|------|---------|
| `0x000000` | bootloader | 32 KB | |
| `0x009000` | nvs | 20 KB | Preferences (settings, odometer, IMU cal) |
| `0x010000` | app0/ota_0 | 3 MB | Primary firmware |
| `0x310000` | app1/ota_1 | 3 MB | OTA target |
| `0x610000` | coredump | 64 KB | Panic dumps |
| `0x620000` | littlefs | ~9.9 MB | GPX tracks / logs (reserved, not yet used) |

LittleFS partition uses subtype `0x82` (labeled `spiffs` in `partitions.csv`) — this is correct and intentional.

## Known Open Issues (v1.2 backlog)

1. **Display performance:** `fillRect()` redraws entire screen every 400ms — partial redraws + TE sync planned for v1.2
2. **Race condition:** `_handleUpdate()` web handler writes to `gErrors` from the async task context without locking
3. **Memory:** static `char buf[16]` stack allocations in hot drawing paths
4. **`drawScreenTerrain`:** 25+ `setTextColor()` calls per frame — should be grouped by color

## Documentation

- `BUILD.md` — detailed build instructions, troubleshooting for display/PSRAM/OTA/encoder/boot failures
- `PROJECT_NOTES.md` — design rationale, calibration values, session log, full roadmap (v1.0–v3.0)
- `CHANGELOG.md` — structured release history (parsed by CI for GitHub Release notes)
- `wiring_diagram_v3.html` — interactive pinout with GPIO annotations
