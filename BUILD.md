# BUILD — Arduino IDE setup

> How to compile and flash this firmware. Update if any library version changes.

---

## 1. Arduino IDE setup

### Install ESP32 board support
- Arduino IDE → File → Preferences
- Additional Board Manager URLs: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
- Tools → Board → Boards Manager → search "esp32" → install **3.0.x or newer**
  (Core 3.x required for new `ledcAttach()` API used in this firmware.)

### Select board
- Tools → Board → ESP32 Arduino → **ESP32S3 Dev Module**

### Board settings (critical!)
| Setting | Value |
|---|---|
| USB CDC On Boot | Enabled |
| CPU Frequency | 240MHz |
| Core Debug Level | None (or Verbose for debugging) |
| **Flash Size** | **16MB (128Mb)** |
| **Flash Mode** | **QIO 80MHz** |
| **PSRAM** | **OPI PSRAM** |
| **Partition Scheme** | **Custom** (uses `partitions.csv` from sketch folder, see §3.1) |
| Upload Speed | 921600 |
| USB Mode | Hardware CDC and JTAG |
| Erase All Flash Before Sketch Upload | Disabled (preserves NVS) |

⚠️ **PSRAM = OPI PSRAM** — без цього `ESP.getFreePsram()` поверне 0 і
GPS track buffer впаде в heap (працювати буде, але обмежено).

⚠️ **Partition Scheme = Custom** — з v1.1.2 проєкт використовує власну
`partitions.csv` (2× 3MB OTA slots + 64KB coredump + ~9.875MB LittleFS).
Стандартні preset'и з Arduino IDE (Huge APP, Default, Minimal SPIFFS) не
підходять: Huge APP не має OTA, Default розрахований на 4MB чіп.

---

## 2. Required libraries

Install via Library Manager (Tools → Manage Libraries) unless noted:

| Library | Author | Purpose | Min version |
|---|---|---|---|
| Arduino_GFX_Library | moononournation | ST77916 display driver | 1.4+ |
| TinyGPSPlus | mikalhart | NMEA parsing | 1.0.3+ |
| Adafruit NeoPixel | Adafruit | WS2812 (replaces FastLED) | 1.12+ |
| ESPAsyncWebServer | me-no-dev or ESP32Async fork | OTA captive portal | latest |
| AsyncTCP | me-no-dev or ESP32Async fork | dependency of above | latest |
| Dusk2Dawn | dmkishi | sunrise/sunset | 1.0.1+ |
| Preferences | (built-in with ESP32 core) | NVS storage | — |
| LittleFS | (built-in with ESP32 core 3.x) | File system (GPX, logs) | — |
| BLEDevice | (built-in with ESP32 core) | BLE Central | — |

**Note on AsyncWebServer**: official me-no-dev fork has known issues with
recent ESP32 cores. If compile fails, use **ESP32Async/ESPAsyncWebServer**
fork instead — same API, better Core 3.x support.

---

## 3. File layout in sketch folder

The Arduino IDE expects all files in one folder named exactly like the .ino:

```
esp32_bike_computer/
├── esp32_bike_computer.ino   (main)
├── partitions.csv            (custom partition table — required)
├── config.h
├── ble_manager.h
├── error_handler.h
├── led_controller.h
├── mpu6500.h
├── ota_update.h
├── settings.h
└── ui_theme.h
```

When you open the .ino, all .h files appear as tabs.

### 3.1 Custom partition table (`partitions.csv`)

The IDE auto-detects `partitions.csv` in the sketch folder when
**Partition Scheme = Custom** is selected. Layout:

| Region | Size | Purpose |
|---|---|---|
| nvs | 20 KB | Preferences (settings, theme, odometer, IMU calibration) |
| otadata | 8 KB | bootloader OTA flag |
| app0 (ota_0) | 3 MB | primary firmware slot |
| app1 (ota_1) | 3 MB | secondary firmware slot (OTA target) |
| coredump | 64 KB | panic dump partition for post-mortem debug |
| littlefs | ~9.875 MB | file system for GPX tracks, logs |
| **Total** | **16 MB** | exact match to N16R8 flash |

LittleFS uses subtype `spiffs` (0x82) — that is the Arduino-ESP32 Core 3.x
convention for backward compatibility. The code calls `LittleFS.begin()` and
the core picks the right driver automatically.

If you change `partitions.csv`, **erase flash before next upload** (Tools →
Erase All Flash Before Sketch Upload → Enabled, just for that one upload).
Otherwise the bootloader may use the old layout cached in `otadata`.

---

## 4. First flash procedure

1. Connect ESP32-S3 via USB-C (use the USB port, not UART).
2. Hold **BOOT** button, press **RESET**, release **BOOT** — board enters bootloader mode.
   (Most ESP32-S3 dev boards do this automatically; manual only if upload fails.)
3. Tools → Port → select the COM/USB device.
4. Sketch → Upload (or Ctrl+U).
5. After flash, the board auto-resets. Open Serial Monitor at **115200** baud.

Expected boot output:
```
[INIT] PSRAM: 8.0 MB available
=== ESP32 Bike Computer v1.1.2 ===
INIT: Display...
INIT: Display OK
INIT: TE ISR attached on GPIO39
INIT: LittleFS...
INIT: LittleFS OK — 0/9876 KB used
INIT: Loading settings...
Odometer: X.XX km
INIT: GPS UART1...
INIT: IMU MPU6500...
[INIT] IMU MPU6500: OK
INIT: WS2812B front...
INIT: Encoder OK
INIT: Buttons BTN1/BTN2/BTN3 (TTP223, active HIGH)
INIT: Battery ADC GPIO7
INIT: BLE Central (Garmin HR)...
INIT: IMU task started on Core 0
INIT: Done. Starting main loop.
```

If anything fails, check the corresponding error in `error_handler.h` and
look at `ERR_NAMES` to find which module needs attention.

---

## 5. OTA flash (after first flash)

1. From settings menu on device: navigate to OTA → Start OTA.
2. ESP creates WiFi AP "BikeComp_OTA" (password "12345678").
3. Connect phone/laptop to that AP.
4. Captive portal opens automatically (or browse to 192.168.4.1).
5. Settings → OTA Update → choose .bin file → Upload.
6. Device reboots into new firmware after flash.

Generate the .bin in Arduino IDE: **Sketch → Export Compiled Binary**.
The .bin appears in the sketch folder under `build/esp32.esp32.esp32s3/`.

---

## 6. Troubleshooting

### "esptool: ImportError" or upload hangs
- Hold BOOT + tap RESET to force bootloader.
- Try lower upload speed (460800 or 115200).
- Check USB cable is data-capable (not charge-only).

### "fatal error: Arduino_GFX_Library.h: No such file"
- Library not installed. Library Manager → search → install.

### Compilation OK but display blank / Random pixels on screen
- **Most common cause**: wrong bus type. ST77916 modules with markings
  `IO1 IO2 IO3 SDA SCL` are **QSPI**, not single-line SPI. Use
  `Arduino_ESP32QSPI` constructor (as in v1.1.1+), not `Arduino_HWSPI`.
- Check `Arduino_GFX_Library` is ≥ 1.4 — `Arduino_ESP32QSPI` was added
  in newer versions. If compilation fails on that constructor, update the lib.
- If colors are inverted (cyan looks orange etc.) — flip the 4th argument
  of `Arduino_ST77916(...)` between `true` and `false` (IPS inversion).
- If stable colors but garbage pixels — drop `TFT_SPI_FREQ` from 40MHz to
  20MHz. On breadboard wiring, QSPI cross-talk between 4 parallel data lines
  can corrupt frames at 40MHz. Production PCB with proper layout handles 40MHz fine.
- Backlight check: `ledcWrite(PIN_TFT_BL, 200)` should light up the panel
  even if nothing is drawn. If not lit — backlight wiring problem.

### PSRAM not detected
- Board setting "PSRAM" must be **OPI PSRAM** (not Disabled, not QSPI).
- Some clone boards have PSRAM but need different setting — try both QSPI/OPI.

### OTA upload fails halfway
- Likely OOM. Confirm BLE.end() is called before AP start (added in v1.1).
- Check partition scheme has enough space for OTA slot (≥1.5MB per slot).

### LittleFS mount fails on first boot
- Expected on first ever flash with new partition layout. Code calls
  `LittleFS.begin(true)` which auto-formats on mount fail. Reboot once and
  the second boot should show "LittleFS OK".
- If it persists across reboots: erase flash (Tools → Erase All Flash Before
  Sketch Upload → Enabled), reflash. The old partition table may be cached.

### "Sketch too big" error
- App slot is 3MB (0x300000). Current firmware should fit comfortably (~2MB).
- If hit: enlarge OTA slots in `partitions.csv` and shrink LittleFS proportionally.
  Both slots must be the same size (constraint of OTA bootloader).

### Encoder direction reversed
- Swap `PIN_ENC_CLK` and `PIN_ENC_DT` in config.h, or invert sign in `ENC_TBL`.
