# Changelog — ESP32 Bike Computer Firmware

Format: each version lists changes by category. Newest at the top.

---

## [1.1.3] — 2026-04-26 — Remove Dusk2Dawn, inline USNO sunrise/sunset

### 🔧 Dependency removed
- **Dusk2Dawn library eliminated.** The library included `<Math.h>` (capital M)
  which caused a fatal compile error on Linux (GitHub Actions, case-sensitive FS):
  `fatal error: Math.h: No such file or directory`. The bug exists in the only
  released version (1.0.1) and has no upstream fix.
- Sunrise/sunset is now computed by an **inline USNO algorithm** directly in
  `esp32_bike_computer.ino` — same algorithm Dusk2Dawn used internally,
  sourced from edwilliams.org/sunrise_sunset_algorithm.htm.

### ✨ New function: `calcSunMinutes()`
- Signature: `calcSunMinutes(year, month, day, lat, lon, utcOffset, dst, isSunrise) → int`
- Returns **minutes from local midnight**, matching the previous Dusk2Dawn API
  (`sr / 60.0f` conversion unchanged in `updateAstronomy()`).
- Returns **`-1`** for polar day / polar night (sun never rises or sets) —
  previously Dusk2Dawn returned `−1` for the same condition; `gSunriseH` is set
  to `0.0f` in this case, keeping the existing `gSunriseH > 0` guard in the UI intact.
- Uses `sinf` / `cosf` / `floorf` / `fmodf` (float variants) — no overhead on Xtensa.
- Accuracy: ±1 min for latitudes ±72°, degrades toward polar circle (irrelevant
  for cycling use).

### 🗑 Build system
- **`build.yml`**: `arduino-cli lib install "Dusk2Dawn"` step removed.
  CI now installs one fewer library and has no Linux/case-sensitivity patch needed.

### 📝 Code hygiene
- Header comment in `esp32_bike_computer.ino` updated: Dusk2Dawn removed from
  required-libraries list, note added explaining the inline replacement.
- `BUILD.md` library table: Dusk2Dawn row can be removed from local installs
  (no action needed — unused library does not affect compilation).

---

## [1.1.2] — 2026-04-26 — File system, partition, NVS wear leveling

### 🗄 File system / Partition table
- **New `partitions.csv`** in sketch folder. 16MB layout:
  - 20KB `nvs` (Preferences)
  - 8KB `otadata` (OTA bootloader flag)
  - **2× 3MB OTA slots** (`ota_0`, `ota_1`) — was previously single Huge APP
  - **64KB `coredump`** — for post-mortem panic analysis
  - **~9.875MB `littlefs`** — new file system partition
- LittleFS mounted at boot via `LittleFS.begin(true)` (auto-format on first run).
  Currently no firmware code writes to it; reserved for GPX track export and
  crash logs in upcoming versions. The partition is allocated now to avoid a
  destructive partition resize later.
- **`BUILD.md`** updated: Partition Scheme = **Custom** (was misleading
  "Huge APP / 16M with FATFS"). Custom partition note added (§3.1).

### 🔧 NVS wear leveling
- **`ODOM_SAVE_KM` increased from `0.1f` → `1.0f`** (10× fewer writes).
  Rationale: at 0.1km granularity, 10 000km of riding = 100k writes to the same
  NVS key — at the documented flash wear limit. 1.0km gives 10× margin while
  costing at most ~1km of mileage on power loss between saves.
- **Three new force-save points** to compensate for the larger step:
  1. On settings menu open — natural checkpoint, user is typically stopped
  2. On `OtaManager::start()` — before BLE teardown / AP start
  3. On `_handleUpdate()` first chunk — right before flash write begins

### 🐛 Bonus bug fix
- **`ERR_NAMES[]` / `ERR_SHORT[]` array misalignment**: latent bug where
  `ERR_BLE_NRF=7` was commented out in the enum but the strings array still
  had 9 elements with no gap, causing `ERR_NAMES[ERR_OTA_FAIL=8]` to return
  "PSRAM" and `ERR_NAMES[ERR_PSRAM=9]` to read out-of-bounds.
  Fixed by filling slot 7 with new `ERR_FS` (which we needed anyway).
  No user-visible impact in v1.1.x because OTA / PSRAM errors rarely fired.

### ✨ New features
- **`PIN_TFT_TE` activated on GPIO 39** — ISR-driven FPS counter.
  Each TE pulse from ST77916 increments `gTeTickCount`; `gDisplayFps` is
  computed once per second. Display loop is *not* yet sync'd to TE — that
  comes in v1.2 with partial redraws. Useful as a metric for tuning
  `TFT_SPI_FREQ` and validating future optimization work.
- **`ERR_FS` error code** added (slot 7) for LittleFS mount/write failures.
  Severity SEV_WARN — bike computer continues to work without FS.

### 📝 Code hygiene
- `settings.h`: `#define SN "settings"` replaced by `#define SN PREF_SETT`
  (uses macro from config.h instead of duplicating the literal).
- `settings.h`: explicit `#include "config.h"` added for `PREF_SETT` dependency.
- `esp32_bike_computer.ino`: header comment refreshed (v1.1.2, partition note,
  LittleFS in lib list).

### 📊 Action required after v1.1.1 → v1.1.2 upgrade
- **Add `partitions.csv` to your sketch folder** (it's a new file).
- **Tools → Partition Scheme → Custom** before next compile.
- **Erase All Flash Before Sketch Upload → Enabled** for *one* upload to
  apply the new partition table (otherwise bootloader uses old layout).
  After that, set it back to Disabled to preserve NVS on subsequent uploads.
- **Wire GPIO 39 → display TE pin** physically (or leave NC — ISR will just
  never fire, no harm). FPS counter will read 0.

---

## [1.1.1] — 2026-04-26 — QSPI display pinout fix

### 🔧 Hardware / wiring
- **Display interface changed**: ST77916 round 360×360 module turned out to use
  **QSPI (4 parallel data lines)**, not regular single-line SPI as initially assumed.
  Module pinout markings: `TE BL CS RST IO3 IO2 IO1 SDA SCL VCC GND` — there is
  no DC pin (command/data is encoded in the QSPI header byte).
- **Pin remapping** in `config.h`:
  - `PIN_TFT_MOSI` (GPIO 11) → renamed to `PIN_TFT_D0`, same GPIO
  - `PIN_TFT_DC` (GPIO 13) → repurposed as `PIN_TFT_D1`, same GPIO
  - `PIN_TFT_D2` (GPIO 15) — **new pin to wire**
  - `PIN_TFT_D3` (GPIO 38) — **new pin to wire**
  - `PIN_TFT_SCK`, `PIN_TFT_CS`, `PIN_TFT_RST`, `PIN_TFT_BL` unchanged
  - `PIN_TFT_TE` defined but commented (optional vsync, NC for now)
- **Action required**: physically wire 2 additional GPIOs (15 → IO2, 38 → IO3).
  GPIO 13 stays connected but to IO1 instead of DC.

### 💻 Software
- `esp32_bike_computer.ino`: bus init switched from `Arduino_HWSPI` to
  `Arduino_ESP32QSPI(cs, sck, d0, d1, d2, d3)`.
- `Arduino_ST77916` constructor: added 4th argument `true` (IPS color inversion)
  — flip to `false` if colors come out inverted on first boot.

### 📊 Expected impact
- Display refresh ~4× faster than single-SPI (4 parallel data lines @ 40MHz).
  This partially addresses the v1.2 TODO about `fillRect` performance.
- No firmware behavioural changes for the user — same screens, same UI.

### 📝 Documentation
- `wiring_diagram_v3.html` regenerated with QSPI pinout.
- `PROJECT_NOTES.md`: added "Чому QSPI замість звичайного SPI" rationale.
- `BUILD.md`: added QSPI troubleshooting section.

---

## [1.1.0] — 2026-04-26 — Code review fixes

### 🔴 Critical fixes
- **Race condition on IMU data**: added `portMUX_TYPE gIMUMux` + `IMUData gIMUSnapshot`.
  All drawing/web code now reads from snapshot, not directly from `gIMU.data`.
- **Race condition on Preferences (NVS)**: IMU calibration moved from web/UI handlers
  into main loop via `volatile bool gCalibRequested` flag. `vTaskSuspend(hIMUTask)`
  wraps the calibration call to prevent concurrent NVS access.
- **MAP encoder modes broken**: zoom/pan modes were unreachable (encoder always
  switched screens). Now `gMapMode>0` routes encoder rotation to scale/offset.
- **`gLastScreenMs` timer conflict**: split into `gLastScrollMs`, `gLastDrawMs`,
  `gLastSbarMs`. Auto-scroll no longer disrupted by display refresh.
- **WiFi+BLE coexistence during OTA**: added `BLEManager::end()` (full deinit)
  called before `WiFi.softAP()`. BLE restarted after `OtaManager::stop()`.
- **`COL_VIOLET` used as RGB565**: was passing enum value (6) instead of
  `PAL565[COL_VIOLET]` in `drawSettOTA`. FW version line was nearly invisible.

### 🟡 Medium fixes
- Buttons now use `INPUT_PULLDOWN` (TTP223 modules) — safety against floating
  pin if module disconnected.
- `resetSession()` helper: now also clears track buffer, bounding box, map state.
  Both web `/reset/session` and `/reset/total` use it.
- `gAvgSpeedAcc` running average bounded — divides by 2 when N>32000 to prevent
  float precision loss / overflow on long sessions.
- `temperatureRead()` wrapped in `#ifdef` with N/A fallback for future Core compatibility.
- `ESP.getFreePsram()` guarded by `psramFound()` check.
- HR zones display: clarified format `Z1<114 Z2<133 ...` instead of confusing colons.

### 🔧 Hardware / wiring
- **Battery ADC divider changed**: 100k+200k → **200k+100k**.
  - `VBAT_RATIO` 1.5 → 3.0
  - At Vbat=4.2V: ADC moves from 2.8V (non-linear region) to 1.4V (linear).
  - Equivalent impedance, current draw, RC filter unchanged.
  - **Action required**: physically swap R1↔R2 on the board.
- GPS pins renamed in `config.h` for clarity:
  - `PIN_GPS_TX` → `PIN_GPS_TX_PIN` (ESP TX → GPS RX, GPIO 17)
  - `PIN_GPS_RX` → `PIN_GPS_RX_PIN` (ESP RX ← GPS TX, GPIO 18)

### 📝 Other
- Documentation: PROJECT_NOTES.md, BUILD.md, CHANGELOG.md added.

---

## [1.0.0] — Earlier — Initial port from previous chat

- 3 main screens (Riding / Map / Terrain & Body)
- 10 settings screens (Display, Time, LED, GPS, IMU, Battery, BLE, HR, OTA, Odometer)
- BLE Central for Garmin HR (GATT 0x180D)
- IMU MPU6500: brake detection, pothole counter, vibration RMS, pitch/roll
- WS2812×16 front LEDs (turn signals, hazard, thank-you, DRL)
- GPS track recording with auto-scaling and speed colour coding
- OTA via WiFi captive portal (192.168.4.1)
- Astronomy: sunrise/sunset via Dusk2Dawn
- Persistence: theme, settings, total odometer, IMU calibration
- All NRF52840 references commented out (rear unit not yet built)
- Successfully compiled but not yet flashed to hardware

---

## Versioning

Format: `MAJOR.MINOR.PATCH`
- **MAJOR** — breaking hardware changes (new modules, pin remap, new schematics)
- **MINOR** — new features, screens, settings; bug fixes that change behaviour
- **PATCH** — pure bug fixes, no behavioural changes for the user

Update `FW_VERSION` in `config.h` before each release.
