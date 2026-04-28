# Changelog — ESP32 Bike Computer Firmware

Format: each version lists changes by category. Newest at the top.

---

## [1.1.4] — 2026-04-27 — BLE crash fix + UI cleanup + crash logger

### 🔴 Critical fixes
- **BLE HR notify callback hardened.** Symptom: enabling Broadcast Heart
  Rate on Garmin watch caused status bar / error overlay to flash, then
  ESP rebooted within ~5 seconds. Suspect cause: `checkModuleBLEHR(true)`
  was called from inside the NimBLE notify task (small ~4KB stack), which
  triggered `gErrors.set()` + `Serial.printf` + potential NVS access.
  Risk of stack overflow or watchdog timeout in BLE task context.
  - Notify CB now does only `parseHR_fast()` + `_hrLastDataMs = millis()`.
  - All error-flag bookkeeping (`checkModuleBLEHR`) moved to `tick()` in main loop.
  - New optional `BLE_DEBUG` flag adds stack/heap watermark logging in CB
    (off by default, enable in `config.h` to confirm root cause).
  - `parseHRMeasurement()` renamed to `parseHR_fast()` (inline, minimal,
    no allocations) to make CB context constraints explicit.

### ✨ New features
- **CrashLogger module** (`crash_logger.h`):
  - On every boot reads `esp_reset_reason()` and appends a line to
    `/boot.log` (LittleFS, ring-buffered, last 50 entries).
  - On abnormal reset (PANIC / INT_WDT / TASK_WDT / WDT / BROWNOUT),
    extracts coredump summary (faulting task, exception PC, top-of-stack
    backtrace) from the `coredump` partition (introduced in v1.1.2) and
    writes a human-readable file to `/crashes/crash_NN.txt`.
  - Coredump partition is erased after summary save so next crash starts
    fresh.
  - Sets persistent `ERR_LAST_PANIC` flag — a red dot on the status bar
    until cleared by user.
- **HR snapshot pattern** (`gHeartRateBpmSnap`): mirrors the IMU snapshot
  approach. Raw `gHeartRateBpm` is written only by BLE callback. All
  display readers use the snapshot, refreshed once per main-loop tick
  via `refreshHRSnapshot()`. Eliminates display flicker from mid-frame
  value changes.
- **S_BATTERY settings screen**: new "CRASH LOG" section showing last
  reset reason (color-coded), boot count, saved crash count.

### 📋 Deferred to future versions
- **Web UI for crash log** (`/crashes`, `/boot-log` endpoints) — TODO,
  currently crashes are viewable via Serial or direct LittleFS file read.

### 🎨 UI cleanup — status bar
- **`fillArc` ring removed.** The white bezel ring at radius 152..178 is gone.
  Status-bar labels are now rendered with transparent backgrounds (using
  `UI_SKIP_COLOR = 0xF81F` magenta sentinel) and overlay directly on
  whatever is below.
- **Date label removed from status bar.** Bottom arc now shows only Time
  (140°), FIX/!FIX (195°), Sat count (220°). Date is now displayed on the
  TIME settings screen as a read-only row.
- **Text size 2 for primary info**: Time, HR, Battery on the status bar
  use `setTextSize(2)` (12×16 px chars) for readability. Indicators
  (Turn arrows, BR, !FIX/FIX, Sat count, error dot) remain `setTextSize(1)`.
- **Text colors per design spec**:
  - Time: white (was black)
  - Sat count: white (was black)
  - HR: white (was pink)
  - Battery: white (was color-coded by percentage)
  - !FIX: red (unchanged)
  - FIX: green (unchanged)
  - Error dot at 12-o'clock: red (unchanged)

### 🚀 Performance
- **`drawArcLabel()` rewritten with canvas blit pattern.** Old code did
  per-pixel `gfx->writePixel()` after rotation — ~720 SPI transactions
  per label × 7 labels = ~5000 transactions × ~10μs = **~50 ms** per
  status-bar redraw on QSPI@40MHz.
  New code rotates pixels into a RAM-only output canvas, then issues
  **one** `draw16bitRGBBitmapWithMask` per label = ~7 SPI bursts total.
  Estimated **~5 ms** per status-bar redraw — 10× speedup. Frees CPU
  for BLE/IMU tasks, reducing chance of WDT trips.
- **UI design tokens** centralized in `config.h`:
  `UI_SKIP_COLOR`, `UI_ARC_*`, `UI_CVS_*`, `UI_OUT_*`, `UI_FLIP_*`.
  Future UI elements should pull dimensions from these macros instead
  of inventing constants.

### 🗑 Removed
- **`drawErrorOverlay()` function deleted.** No more red error banner
  overlaid on top of the 3 main screens. Active errors are now only
  indicated by the red dot on the status bar (12-o'clock position).
  Future plan (v1.2.0 TODO): per-module error display inside the relevant
  Settings screen — see `PROJECT_NOTES.md` "Error display in settings".

### 📝 Code hygiene
- `error_handler.h`: new `ERR_LAST_PANIC = 10` code with severity
  `SEV_WARN`, ERR_NAMES/ERR_SHORT updated, ERR_COUNT bumped.
- `ble_manager.h`: `parseHRMeasurement` → `parseHR_fast` (inline).
  Snapshot var `gHeartRateBpmSnap` declared next to raw `gHeartRateBpm`.
- `config.h`: FW_VERSION → 1.1.4. New UI design token block at bottom.
- `settings.h`: docstring updated, S_TIME drawing function moved to
  `.ino` (where the rest of `drawSett*` lives). `SETT_LINES[S_TIME]=2`
  unchanged — date row is read-only above active rows.
- `esp32_bike_computer.ino`: header comment refreshed to v1.1.4. New
  `#include "crash_logger.h"`. New `CrashLogger gCrash;` global.

### 📊 Action required after v1.1.3 → v1.1.4 upgrade
- **Pull all 5 modified files**: `config.h`, `error_handler.h`,
  `ble_manager.h`, `settings.h`, `esp32_bike_computer.ino`.
- **Add new file**: `crash_logger.h` to sketch folder.
- **Optional**: enable `BLE_DEBUG` in `config.h` for the first session
  after upgrade to confirm BLE callback stack/heap is healthy.
- **No partition table changes** — existing `partitions.csv` already has
  the `coredump` partition (added in v1.1.2).

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
- `BUILD.md` library table: Dusk2Dawn row removed.

### 📖 Documentation / Flash procedure
- **`BUILD.md` §4.1 added**: manual flash instructions using `esptool` for
  pre-built CI/Release binaries. Documents the `--flash-mode keep` requirement
  — CI bootloader uses DIO mode; patching to QIO with esptool causes
  `ets_loader.c 78` boot failure. Arduino IDE is unaffected (patches on-the-fly).
- **`BUILD.md` troubleshooting**: added `ets_loader.c 78` section.
- **`BUILD.md`**: expected boot output updated to v1.1.3.
- **`README.md`**: version bumped to v1.1.3, roadmap updated.

---

## [1.1.2] — 2026-04-26 — File system, partition, NVS wear leveling

(unchanged — see previous CHANGELOG entry)

---

## [1.1.1] — 2026-04-26 — QSPI display pinout fix

(unchanged — see previous CHANGELOG entry)

---

## [1.1.0] — 2026-04-26 — Code review fixes

(unchanged — see previous CHANGELOG entry)

---

## [1.0.0] — Earlier — Initial port from previous chat

(unchanged — see previous CHANGELOG entry)

---

## Versioning

Format: `MAJOR.MINOR.PATCH`
- **MAJOR** — breaking hardware changes (new modules, pin remap, new schematics)
- **MINOR** — new features, screens, settings; bug fixes that change behaviour
- **PATCH** — pure bug fixes, no behavioural changes for the user

Update `FW_VERSION` in `config.h` before each release.
