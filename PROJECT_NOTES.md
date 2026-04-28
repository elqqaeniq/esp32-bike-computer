# PROJECT_NOTES — ESP32 Bike Computer

## 🎯 Поточний стан
**v1.1.4** — BLE crash fix + UI cleanup + CrashLogger.

Гілка фокусу: **діагностика + стабільність** перед польовими тестами.
Після v1.1.4 — v1.2.0 (архітектурний рефакторинг + польові тести).

---

## 📋 Roadmap

### v1.0 — first compile (попередній чат)
- [x] Дисплей, GPS, IMU, BLE HR, WS2812 фронт
- [x] 3 основні екрани + 10 налаштувань
- [x] OTA через captive portal
- [x] Збереження одометра та калібровки

### v1.1 — Code review fixes
- [x] Виправити критичні race conditions
- [x] Виправити MAP encoder logic
- [x] Виправити VBAT divider
- [x] Виправити COL_VIOLET та інші дрібниці

### v1.1.1 — QSPI display fix
- [x] Перепризначити піни дисплея під реальний QSPI-модуль
- [x] Замінити `Arduino_HWSPI` на `Arduino_ESP32QSPI` в .ino
- [x] Оновити wiring diagram (v3)

### v1.1.2 — File system + NVS wear leveling
- [x] Кастомна `partitions.csv` (2× 3MB OTA + 64KB coredump + ~9.875MB LittleFS)
- [x] LittleFS init у setup()
- [x] `ODOM_SAVE_KM` 0.1 → 1.0 + force-save у трьох точках
- [x] `PIN_TFT_TE` ISR FPS counter
- [x] `ERR_FS` error code + bonus fix `ERR_NAMES[]` alignment
- [x] `settings.h` SN PREF_SETT consistency cleanup

### v1.1.3 — Dusk2Dawn removal + first successful flash
- [x] Dusk2Dawn замінено на inline USNO `calcSunMinutes()`
- [x] CI build виправлено
- [x] Перша успішна прошивка на залізо через `esptool`
- [x] Документація: BUILD.md §4.1 (manual flash), troubleshooting `ets_loader.c 78`
- [x] `--flash-mode keep` задокументовано

### v1.1.4 — BLE crash fix + UI cleanup + crash logger ✅ (поточна)
- [x] **BLE HR notify CB hardened** — винесено `checkModuleBLEHR` у tick()
- [x] **`gHeartRateBpmSnap` snapshot pattern** (як для IMU)
- [x] **`BLE_DEBUG` стек/heap watchdog** для діагностики (#ifdef opt-in)
- [x] **CrashLogger module**: `/boot.log`, `/crashes/crash_NN.txt`, `ERR_LAST_PANIC`
- [x] **UI cleanup**: видалено `fillArc` ring, дату — з status bar на S_TIME
- [x] **Status bar**: textSize 2 для time/HR/batt, білий колір; size 1 для індикаторів
- [x] **`drawArcLabel` оптимізація** через canvas blit (~10× швидше)
- [x] **UI design tokens** у `config.h` (`UI_SKIP_COLOR`, `UI_ARC_*`, `UI_CVS_*`)
- [x] **Видалено `drawErrorOverlay()`** — більше нема банера на головних екранах
- [x] **S_BATTERY**: показ last reset reason + crash count

### v1.2.0 — Архітектурний рефакторинг + польові тести
**Фокус**: модульність, dual-core, стабільність, енергоефективність.

#### Архітектура — розділення .ino на модулі
- [ ] Рішення: Arduino IDE чи PlatformIO? (PlatformIO = повний контроль build)
- [ ] `display_screens.cpp/h` ← drawScreenRiding/Map/Terrain з .ino
- [ ] `display_statusbar.cpp/h` ← drawStatusBar, drawArcLabel, canvas mgmt
- [ ] `display_settings.cpp/h` ← усі 10 drawSett*() функцій
- [ ] `gps_manager.cpp/h` ← GPS update logic, calcSunMinutes(), astronomy
- [ ] `input_handler.cpp/h` ← encoder rotation, button reads, debounce
- [ ] `app_globals.cpp/h` ← усі extern глобальні змінні в одному місці
- [ ] `main.ino` ← тільки setup() + loop() (~100 рядків)

#### Dual-core розподіл (FreeRTOS)
- [ ] Core 0 (PRO_CPU) — "Sensor & Comms": BLE, GPS UART, IMU sampling
- [ ] Core 1 (APP_CPU) — "UI & Storage": display, encoder, settings, OTA, LittleFS
- [ ] Комунікація: atomic snapshots (як gHeartRateBpmSnap), FreeRTOS queues
- [ ] Дослідити light_sleep між тіками коли велосипед стоїть

#### Рефакторинг існуючих .h файлів
- [ ] `error_handler.h` → .cpp/.h: static масиви ERR_NAMES[] у .cpp,
      _e[ERR_COUNT] з прямим індексом O(1) замість лінійного пошуку
- [ ] `settings.h` → розділити: settings_data.h (structs), settings_logic.cpp
      (rotate/edit), settings_persist.cpp (NVS)
- [ ] `ble_manager.h`: reconnect exponential backoff (5s→10→20→60s cap),
      fix foundHR raw pointer leak (std::unique_ptr)
- [ ] `mpu6500.h`: begin() повертає bool (зараз void)
- [ ] `led_controller.h`: LED show() на Core 1 (подалі від BLE)
- [ ] `ota_update.h`: при OTA — GPS pause, OTA на Core 1
- [ ] `config.h`: розбити на #pragma region секції
- [ ] `ui_theme.h`: constexpr де можливо

#### Web UI (deferred з v1.1.4)
- [ ] `/crashes` endpoint — список crash файлів
- [ ] `/boot-log` endpoint — останні 50 boots
- [ ] `/crashes/clear` POST — видалення crash файлів
- [ ] `/crashes/view?f=...` — перегляд окремого crash файлу
- [ ] Нові методи у crash_logger.h для web UI (listCrashes, readCrash, readBootLog)

#### Фічі
- [ ] **Експорт треку у GPX** через web UI (LittleFS готовий)
- [ ] **Error display в settings menu** — per-module секції (див. "Ідеї" нижче)
- [ ] Реалізувати ідею покращення ODOM_SAVE (deferred з v1.1.2)
- [ ] TE-driven partial redraws

#### Польові тести
- [ ] Розпаяти на breadboard / у корпусі
- [ ] First boot: дисплей, GPS lock, IMU read
- [ ] Тест BLE до Garmin Forerunner 255s (re-test після v1.1.4 fix)
- [ ] Тест OTA upload через captive portal
- [ ] Польова калібровка BRAKE_THRESHOLD/MAX
- [ ] Польова калібровка POTHOLE_G
- [ ] Виміряти реальний impact display partial redraws (з TE синхронізацією)

### v2.0 — NRF активація
- [ ] Прошити NRF52840 (rear unit)
- [ ] Розкоментувати NRF код в усіх 5 файлах
- [ ] Тест BLE link ESP↔NRF на дальність
- [ ] Інтеграція ANT+ cadence (Magene S3)

### v3.0 — Nice to have + future ideas
- [ ] Графік висоти / швидкості за сесію
- [ ] Багатотрек: вибір з кеша на пристрої
- [ ] Crash detection (різке падіння az + зупинка)
- [ ] Stand-by mode при бездіяльності
- [ ] TE-driven partial redraws для anti-tearing
- [ ] **GPX track upload to Google Drive** через домашній WiFi
      (memory note from prior session)
- [ ] **Garmin-compatible `.fit` activity files** (track + HR + cadence)
      для імпорту у Garmin Connect / Strava (memory note from prior session)

---

## 💡 Ідеї до обговорення (deferred)

### Error display in settings (v1.2.0)
Зараз (з v1.1.4) активна помилка показується тільки червоною точкою у статус
барі. Повний список — через Serial або S_BATTERY settings screen.

**План на v1.2.0**: кожен settings screen показує помилки релевантного модуля
у відповідній секції на самому екрані (без overlay):
- `S_GPS` → `ERR_GPS` ("GPS: no data >5s")
- `S_IMU` → `ERR_IMU` ("I2C not found 0x68")
- `S_BLE` → `ERR_BLE_HR` ("Garmin not found")
- `S_BATTERY` → `ERR_VBAT_ADC`, `ERR_PSRAM`, `ERR_FS`, `ERR_LAST_PANIC`
- `S_OTA` → `ERR_OTA_FAIL`

**Додатковий індикатор**: позначка `!` поряд з назвою settings screen у header
(наприклад `IMU !`) — якщо для цього модуля є active error. Дозволяє користувачу
знайти "де помилка" швидше, не сканувати всі 10 екранів.

Реалізація: helper-функція у `settings.h`/`error_handler.h`:
```cpp
inline ErrorCode firstActiveForScreen(SettScreen s) {
  switch(s) {
    case S_GPS:     return gErrors.isActive(ERR_GPS) ? ERR_GPS : ERR_NONE;
    case S_IMU:     return gErrors.isActive(ERR_IMU) ? ERR_IMU : ERR_NONE;
    // ...
  }
}
```

### Покращення ODOM_SAVE (v1.2.0)
Користувач має ідею щодо подальшої оптимізації збереження одометра.
Контекст: зараз save раз на 1км + force-save на settings open / OTA start /
OTA flash final. Повернутись до обговорення перед v1.2 implementation.

### TE-driven anti-tearing (v3.0)
Зараз TE — лише FPS counter. Повна синхронізація вимагає: чекати на TE
RISING edge перед `gfx->fillRect`/`pushPixels`. Зробити після того як
буде partial redraws (без них немає сенсу — full screen redraw 252KB
все одно займає кілька кадрів TE).

---

## 🧪 Калібровані значення (експериментальні)
| Параметр | Значення | Джерело | Дата |
|---|---|---|---|
| BRAKE_THRESHOLD | -0.30g | TODO: підібрати на велосипеді | — |
| BRAKE_MAX | -0.70g | TODO: підібрати | — |
| POTHOLE_G | 3.0g | TODO: підібрати | — |
| GPS_SLOPE_WIN_M | 15m | компроміс шум/реакція | — |
| VBAT_RATIO | 3.0 | дільник 200k+100k (виправлено) | 2026-04-26 |
| ODOM_SAVE_KM | 1.0km | wear leveling, було 0.1km у v1.1.1 | 2026-04-26 |

---

## 🔌 Pinout (ESP32-S3-N16R8)
Деталі див. `wiring_diagram_v3.html`. Резерви:
- GPIO 26-37: flash + PSRAM (НЕ ЧІПАТИ)
- GPIO 45, 48: strapping/diff (FastLED invalid)
- Вільні: 40, 41, 42, 47 (з обмеженнями для USB на 19/20)
- GPIO 39 — використано для TE pin з v1.1.2

Display QSPI naming convention (з v1.1.1):
- `PIN_TFT_D0 = 11` — SDA (MOSI у single-SPI режимі)
- `PIN_TFT_D1 = 13` — IO1
- `PIN_TFT_D2 = 15` — IO2
- `PIN_TFT_D3 = 38` — IO3
- `PIN_TFT_SCK = 12`, `PIN_TFT_CS = 10`, `PIN_TFT_RST = 14`, `PIN_TFT_BL = 21`
- `PIN_TFT_TE = 39` — Tearing Effect (FPS counter в v1.1.2)

GPS naming convention (з v1.1):
- `PIN_GPS_TX_PIN = 17` — ESP TX → GPS RX
- `PIN_GPS_RX_PIN = 18` — ESP RX ← GPS TX

Encoder + Buttons (з config.h):
- `PIN_ENC_CLK = 4`, `PIN_ENC_DT = 5`, `PIN_ENC_SW = 6`
- `PIN_BTN1 = 1` (LEFT), `PIN_BTN2 = 2` (RIGHT), `PIN_BTN3 = 3` (TY/HAZARD)

---

## 🎨 UI Design Tokens (з v1.1.4)

Усі magic numbers для UI — у `config.h` під заголовком "UI DESIGN TOKENS".
Не дублювати константи в інших файлах. Якщо треба новий розмір тексту або
радіус — додавай у config.h і використовуй макрос.

| Токен | Значення | Призначення |
|---|---|---|
| `UI_SKIP_COLOR` | `0xF81F` (magenta) | sentinel для прозорого фону canvas |
| `UI_ARC_CX/CY` | 180/180 | центр кругу для status-bar лейблів |
| `UI_ARC_R_MID` | 165 | радіус позиціонування лейблів |
| `UI_CVS_W_S1/H_S1` | 60/12 | input canvas для textSize 1 |
| `UI_CVS_W_S2/H_S2` | 96/20 | input canvas для textSize 2 |
| `UI_OUT_S1/S2` | 68/98 | output canvas (rotated bbox) |
| `UI_FLIP_LO/HI_DEG` | 90/270 | діапазон кутів для flip 180° |

Канвас blit pattern (з v1.1.4): малюємо текст у RAM canvas, повертаємо у
вторинний RAM canvas, потім один `draw16bitRGBBitmapWithMask` на дисплей.
Це ~10× швидше за per-pixel writePixel.

---

## 📚 Корисні посилання
- ST77916 datasheet: [TODO додати]
- MPU6500 register map: invensense.com
- ATGM336H docs: [TODO додати]
- Garmin HR profile: bluetooth.com GATT 0x180D
- ESP-IDF coredump API: docs.espressif.com → Core Dump

---

## 💬 Журнал сесій

### 2026-04-28 — v1.1.4 фіналізація + архітектурний план v1.2.0
- Web UI для crash log відкладено (методи видалено з crash_logger.h, записано в TODO).
- Креші та логи поки тільки serial + LittleFS flash.
- Складено план архітектурного рефакторингу v1.2.0:
  - Розділення .ino на 7 модульних .cpp/.h файлів.
  - Dual-core: Core 0 = сенсори (BLE/GPS/IMU), Core 1 = UI + storage.
  - Поради по рефакторингу кожного .h файлу (error_handler O(1), settings split,
    BLE backoff, mpu6500 begin() bool, LED на Core 1, OTA GPS pause).
  - Записано у TODO v1.2.0, обговоримо пізніше.

### 2026-04-27 — v1.1.4 — BLE crash fix + UI cleanup + crash logger
- BLE crash при увімкненні Broadcast HR на годиннику ребутить ESP за ~5с.
- Гіпотеза: `checkModuleBLEHR(true)` з NimBLE notify CB context (~4KB stack)
  → stack overflow або WDT. Винесли усе error-bookkeeping у `tick()`.
- Додано `BLE_DEBUG` опт-ін для логування stack/heap watermark у CB.
- Створено `crash_logger.h`: boot reason → `/boot.log`, coredump summary →
  `/crashes/crash_NN.txt`. Тільки serial + flash (web UI — TODO).
- UI: видалено `fillArc` біле кільце безеля, дата прибрана зі status bar
  (перенесена на S_TIME як read-only рядок).
- Status bar: textSize 2 для time/HR/batt, білий колір; size 1 для індикаторів.
- `drawArcLabel` переписано через canvas blit pattern (один blit замість
  ~720 writePixel per label) — ~10× швидше, знижує тиск на BLE/IMU тасків.
- Видалено `drawErrorOverlay()` повністю. Залишається тільки червона точка
  на 12-год позиції безеля. Повне відображення помилок переходить у settings
  у v1.2.0 (per-module sections).
- Записано в TODO: error display в settings menu, зміна архітектури коду.

### 2026-04-26 — v1.1.3 — First successful flash to hardware
- Перша спроба через **esptool-js** (web flasher) — boot loop `ets_loader.c 78`.
  Причина: web flasher патчив flash mode byte в bootloader header на QIO,
  але CI-compiled bootloader зібраний з DIO.
- BUILD.md: Partition Scheme = Custom, додано §3.1 про partitions.csv,
  оновлено troubleshooting (LittleFS, "Sketch too big").

### 2026-04-26 — v1.1.1 — QSPI display
- При першому заливі прошивки виявилось, що маркування модуля
  (`TE BL CS RST IO3 IO2 IO1 SDA SCL VCC GND`) не відповідає single-SPI.
  Це QSPI-дисплей (4 паралельні лінії даних, без DC).
- Перепризначені піни: GPIO 13 з DC → D1, додано GPIO 15 (D2), GPIO 38 (D3).

### 2026-04-26 — v1.1 fixes
- Code review: знайдено 7 критичних, 8 mid, 5+ optimization issues.
- Виправлено всі критичні + більшість mid.

### 2026-04-26 — перенесено в проект
- Перенесено контекст з попереднього чату (6 частин).
- Файли v1.0 успішно компілювались, але не прошивались на залізо.
