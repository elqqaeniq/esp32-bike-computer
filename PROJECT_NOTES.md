# Bike Computer — Project Notes

> Living document. Update after every working session.
> Last updated: 2026-04-26 (v1.1.3)

---

## 🎯 Що це
ESP32-S3-N16R8 велокомп'ютер з круглим дисплеєм 360×360, GPS, IMU, BLE HR (Garmin),
WS2812 фронтальними поворотниками. NRF52840 задній модуль (24 LED + cadence ANT+) —
**plan**, поки відключений у прошивці.

---

## 🏗 Архітектурні рішення

### Чому inline USNO замість Dusk2Dawn (v1.1.3)
Dusk2Dawn v1.0.1 містить `#include <Math.h>` (велика M) у своєму заголовку.
На Windows файлова система case-insensitive — компілюється. На Linux (GitHub Actions)
`Math.h ≠ math.h` → `fatal error: Math.h: No such file or directory`. Баг
присутній в єдиній релізній версії бібліотеки, без upstream виправлення.

Рішення: прибрати залежність повністю. Sunrise/sunset — це детерміновані астрономічні
формули (алгоритм USNO), без зовнішнього стану, без ініціалізації. 50 рядків
`calcSunMinutes()` замінюють бібліотеку повністю. Той самий алгоритм, який
Dusk2Dawn використовував всередині (edwilliams.org). Точність ±1 хв до ±72° широти.

Полярна ніч/день: `calcSunMinutes()` повертає `-1` → `gSunriseH = 0.0f` →
UI показує "No fix" (перевірка `gSunriseH > 0` залишилась незмінною).

### Чому LittleFS + кастомна partition table (v1.1.2)
Стандартні preset'и Arduino IDE не підходять:
- "Huge APP (3MB No OTA)" — без OTA взагалі (а ми робимо captive portal upload)
- "Default 4MB" — для 4MB чіпа, не 16MB
- "16M Flash with FATFS" — є, але FAT має великий overhead і немає журналювання

Власна `partitions.csv`: 2× 3MB OTA + 64KB coredump + ~9.875MB LittleFS.
LittleFS обраний над SPIFFS (deprecated в IDF 5.x) і FATFS (overhead, no journal):
має справжні директорії, журналювання (стійкість до raptовогo вимикання — для
велокомпа критично), wear-leveling, нормальну швидкість.

Партиція виділена *зараз*, навіть без активного використання — щоб уникнути
destructive partition resize при додаванні GPX експорту в v1.2/v2.x.
Зміна partition table при OTA вимагає ручного "Erase All Flash" — ми це
робимо один раз при переході 1.1.1→1.1.2.

### Чому ODOM_SAVE_KM 1.0 замість 0.1 (v1.1.2)
NVS на ESP32 використовує ту саму flash що і код — обмежена кількість write
cycles на page (~100k за специфікацією, реально 5-10× більше). Зберігаючи
`total_km` кожні 100м, за 10000км пробігу ми мали б 100k записів того самого
ключа — впритул до межі.

Збільшено до 1км + force-save у трьох додаткових точках (settings open,
OTA start, OTA flash final). Втрата при power loss зросла з ≤100м до ≤1км,
але реальна частина пробігу між зупинками зазвичай зберігається через
settings checkpoint. Ресурс flash збільшено в 10×.

### Чому TE pin лише як FPS counter (v1.1.2)
TE (Tearing Effect) виходить з контролера дисплея кожен раз як завершується
внутрішній refresh кадру (~60 Hz). У повному використанні — синхронізація
gfx writes з TE усуває tearing. Але це вимагає переписати display loop
(чекати TE перед `fillRect`/`pushPixels`).

Поки що — ISR-counter, що рахує реальний FPS. Корисно як метрика для тюнінга
`TFT_SPI_FREQ` та для валідації виграшу від partial redraws у v1.2.
Інтеграцію в draw loop робимо разом з partial redraws (один заплутаний
рефакторинг краще ніж два).

### Чому QSPI замість звичайного SPI для дисплея
Маркування фізичного модуля `TE BL CS RST IO3 IO2 IO1 SDA SCL` — це signature
QSPI-дисплея. ST77916 на круглих 360×360 модулях майже завжди йде у QSPI
режимі (single-line SPI занадто повільний для 360×360@RGB565 = 252KB/frame).

Ключова відмінність: **DC-піна немає**. Команда vs дані розрізняються бітом
у заголовку SPI-кадру (24-bit header перед payload), а не апаратним сигналом.
Тому стара ініціалізація `Arduino_HWSPI(DC, CS, SCK, MOSI, ...)` не підходить
— потрібен `Arduino_ESP32QSPI(CS, SCK, D0, D1, D2, D3)`.

Перепризначення пінів (v1.1.1):
- GPIO 11 (MOSI) → D0 (та сама лінія, бо D0 у QSPI = MOSI у single-SPI)
- GPIO 13 (DC)   → D1 (DC більше не потрібен, пін повторно використано)
- GPIO 15        → D2 (новий)
- GPIO 38        → D3 (новий)

Перевірені вільні GPIO на N16R8 з OPI PSRAM: 15, 38, 39, 40, 41, 42, 47.
GPIO 26-37 зайняті flash+PSRAM, 45/48 — strapping.

Пропускна стане ~4× вища. Це частково розв'язує TODO про повільний `fillRect`
з v1.2 roadmap (без переписування всього drawing layer).

### Чому Adafruit_NeoPixel замість FastLED
FastLED на ESP32-S3 має пін-валідацію, що відмовляє на GPIO16 (хоча він валідний).
NeoPixel використовує RMT драйвер без обмежень. Дата на GPIO16, не чіпай.

### Чому NRF поки відключений
Хочу спочатку підтвердити роботу основного модуля на велосипеді.
Усі NRF-related виклики закоментовані з префіксом `// NRF disabled:`.
План активації:
1. Розкоментувати `BLE_NRF_*` у `config.h`
2. Розкоментувати NRF гілки у `ble_manager.h` (foundNRF, _connectNRF, characteristics)
3. Розкоментувати `_sendNRFCmd` body в `led_controller.h`
4. Розкоментувати S_LED rear rows у `settings.h`
5. Розкоментувати UI поля в `ota_update.h` для rear LED settings

### Чому Y-вісь IMU = forward
Кріплення MPU6500 на корпусі. Y → forward → ay зчитує гальмування (від'ємний при гальмуванні).

### Чому ADC дільник 200k+100k (раніше 100k+200k)
Vbat=4.2V → ADC=1.40V — середина лінійного діапазону ESP32-S3 ADC1@11dB.
Стара схема (100k+200k) давала ADC=2.8V, де нелінійність 3-5% і нефіксується коефіцієнтом.
Equivalent impedance залишилось 66.7kΩ, 100nF cap працює так само, струм спокою ~14μA.
`VBAT_RATIO` змінений з 1.5 на 3.0 у config.h.

### Чому INPUT_PULLDOWN на кнопках
Сенсорні модулі TTP223 — push-pull вихід, активно тримає HIGH/LOW коли живиться.
Однак якщо модуль фізично відключений (а провід ще йде до GPIO) — pin floating
→ випадкові спрацювання. PULLDOWN як safety net (TTP223 push-pull сильніший
за внутрішній 45kΩ pulldown — конфлікту немає).

### Чому IMU через snapshot під spinlock
IMU task на Core 0 пише в `gIMU.data` 50 разів/сек. Drawing/loop/web-handler
читають з Core 1. Структура з float — не атомарна → tearing reads.
Рішення: `portMUX_TYPE` + `IMUData gIMUSnapshot` що оновлюється раз за loop.
Усі read-сайди (drawing, OTA web) використовують snapshot, ніколи `gIMU.data` напряму.

### Чому BLE deinit перед OTA
На ESP32-S3 BLE+WiFi coexistence працює, але RAM ~30-40KB на BLE stack +
AsyncWebServer + Update buffer = ризик OOM при upload .bin (1MB+).
`gBLE.end()` робить deinit перед `WiFi.softAP()`, після `stop()` — `begin()`.

---

## 🐛 Виправлені баги (v1.1)
- [x] `gLastScreenMs` конфлікт між auto-scroll і display refresh → розділено на 3 змінні
- [x] MAP режими (zoom/pan) не працювали → реалізовано в handleEncoder
- [x] `COL_VIOLET` передавався як колір замість `PAL565[COL_VIOLET]` у drawSettOTA
- [x] Race condition на `gIMU.data` (Core 0 пише, Core 1 читає) → snapshot під spinlock
- [x] Race condition на `Preferences` між IMU task і loop → calibration через flag
- [x] BLE+WiFi одночасно під час OTA → BLE deinit перед AP start
- [x] Кнопки без INPUT_PULLDOWN → додано safety pulldown
- [x] `gTrackLen` не скидався при reset session → resetSession() helper
- [x] `gAvgSpeedAcc` overflow через ~10 годин → ділення на 2 коли N>32000
- [x] `temperatureRead()` deprecated → захищений #ifdef з fallback
- [x] PSRAM показ без перевірки → `psramFound()` guard

## 🐛 Відкриті баги (TODO для v1.2)
- [ ] Display performance: `fillRect` весь area при кожному кадрі — повільно
      (~25ms на 40MHz SPI). Зробити partial redraws тільки на змінах.
- [ ] `_handleUpdate()` пише в `gErrors` з web server task — race з основним потоком
- [ ] Буфери `char buf[16]` у hot path drawing — винести як static
- [ ] `drawScreenTerrain` ~25 setTextColor викликів за кадр — згрупувати

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

## 📋 Roadmap

### v1.0 — first compile (попередній чат)
- [x] Дисплей, GPS, IMU, BLE HR, WS2812 фронт
- [x] 3 основні екрани + 10 налаштувань
- [x] OTA через captive portal
- [x] Збереження одометра та калібровки

### v1.1 — Code review fixes (виправлення критичних багів)
- [x] Виправити критичні race conditions
- [x] Виправити MAP encoder logic
- [x] Виправити VBAT divider
- [x] Виправити COL_VIOLET та інші дрібниці

### v1.1.1 — QSPI display fix
- [x] Перепризначити піни дисплея під реальний QSPI-модуль
- [x] Замінити `Arduino_HWSPI` на `Arduino_ESP32QSPI` в .ino
- [x] Оновити wiring diagram (v3)

### v1.1.2 — File system + NVS wear leveling (поточна, готова до прошивки)
- [x] Кастомна `partitions.csv` (2× 3MB OTA + 64KB coredump + ~9.875MB LittleFS)
- [x] LittleFS init у setup() (без активного використання поки що)
- [x] `ODOM_SAVE_KM` 0.1 → 1.0 + force-save у трьох точках
- [x] `PIN_TFT_TE` ISR FPS counter
- [x] `ERR_FS` error code + bonus fix `ERR_NAMES[]` alignment
- [x] `settings.h` SN PREF_SETT consistency cleanup

### v1.2 — Польові тести + перші features на FS
- [ ] Розпаяти на breadboard / у корпусі
- [ ] First boot: дисплей, GPS lock, IMU read
- [ ] Тест BLE до Garmin Forerunner 255s
- [ ] Тест OTA upload через captive portal
- [ ] Польова калібровка BRAKE_THRESHOLD/MAX
- [ ] Польова калібровка POTHOLE_G
- [ ] Виміряти реальний impact display partial redraws (з TE синхронізацією)
- [ ] **Експорт треку у GPX** через web UI (тепер можливо через LittleFS)
- [ ] Crash log → LittleFS (читання з web `/logs`)
- [ ] Реалізувати ідею покращення ODOM_SAVE (deferred from v1.1.2 — обговорити)

### v2.0 — NRF активація
- [ ] Прошити NRF52840 (rear unit)
- [ ] Розкоментувати NRF код в усіх 5 файлах
- [ ] Тест BLE link ESP↔NRF на дальність
- [ ] Інтеграція ANT+ cadence (Magene S3)

### v3.0 — Nice to have
- [ ] Графік висоти / швидкості за сесію (рендер з LittleFS треку)
- [ ] Багатотрек: вибір з кеша на пристрої
- [ ] Crash detection (різке падіння az + зупинка)
- [ ] Stand-by mode при бездіяльності
- [ ] TE-driven partial redraws для anti-tearing

---

## 🔌 Pinout (ESP32-S3-N16R8)
Деталі див. `wiring_diagram_v3.html`. Резерви:
- GPIO 26-37: flash + PSRAM (НЕ ЧІПАТИ)
- GPIO 45, 48: strapping/diff (FastLED invalid)
- Вільні: 40, 41, 42, 47 (з обмеженнями для USB на 19/20)
- GPIO 39 — використано для TE pin з v1.1.2

Display QSPI naming convention (з v1.1.1, доповнено TE у v1.1.2):
- `PIN_TFT_D0 = 11` — SDA (MOSI у single-SPI режимі)
- `PIN_TFT_D1 = 13` — IO1 (раніше DC у v1.0/1.1)
- `PIN_TFT_D2 = 15` — IO2 (новий пін)
- `PIN_TFT_D3 = 38` — IO3 (новий пін)
- `PIN_TFT_SCK = 12`, `PIN_TFT_CS = 10`, `PIN_TFT_RST = 14`, `PIN_TFT_BL = 21`
- `PIN_TFT_TE = 39` — Tearing Effect input (FPS counter в v1.1.2)

GPS naming convention (з v1.1):
- `PIN_GPS_TX_PIN = 17` — ESP TX → GPS RX
- `PIN_GPS_RX_PIN = 18` — ESP RX ← GPS TX

---

## 📚 Корисні посилання
- ST77916 datasheet: [TODO додати]
- MPU6500 register map: invensense.com
- ATGM336H docs: [TODO додати]
- Garmin HR profile: bluetooth.com GATT 0x180D

---

## 💡 Ідеї до обговорення (deferred)

### Покращення ODOM_SAVE (v1.2)
Користувач має ідею щодо подальшої оптимізації збереження одометра.
Контекст: зараз save раз на 1км + force-save на settings open / OTA start /
OTA flash final. Повернутись до обговорення перед v1.2 implementation.

### TE-driven anti-tearing (v3.0)
Зараз TE — лише FPS counter. Повна синхронізація вимагає: чекати на TE
RISING edge перед `gfx->fillRect`/`pushPixels`. Зробити після того як
буде partial redraws (без них немає сенсу — full screen redraw 252KB
все одно займає кілька кадрів TE).

---

## 💬 Журнал сесій

### 2026-04-26 — v1.1.2 — File system + NVS wear leveling
- Обговорили partition table, файлові системи (LittleFS vs SPIFFS vs FATFS).
- Створено `partitions.csv`: 2× 3MB OTA + 64KB coredump + ~9.875MB LittleFS.
- LittleFS init у setup() — поки без активного використання, але партиція
  є для майбутніх GPX треків / crash logs (без destructive partition resize).
- `ODOM_SAVE_KM` 0.1 → 1.0 + force-save у settings open / OTA start / final.
  10× менше зносу NVS, коректний ресурс flash для довгих пробігів.
- `PIN_TFT_TE = GPIO 39` активовано як ISR FPS counter.
- `ERR_FS` додано в slot 7 → попутно виправлено латентний bug `ERR_NAMES[]`
  alignment (закоментований `ERR_BLE_NRF=7` створював зсув на ±1 для OTA/PSRAM).
- `settings.h` SN → PREF_SETT, explicit include config.h.
- BUILD.md: Partition Scheme = Custom, додано §3.1 про partitions.csv,
  оновлено troubleshooting (LittleFS, "Sketch too big").
- Запам'ятати: користувач має ідею покращення ODOM_SAVE на v1.2.

### 2026-04-26 — v1.1.1 — QSPI display
- При першому заливі прошивки виявилось, що маркування модуля
  (`TE BL CS RST IO3 IO2 IO1 SDA SCL VCC GND`) не відповідає single-SPI.
  Це QSPI-дисплей (4 паралельні лінії даних, без DC).
- Перепризначені піни: GPIO 13 з DC → D1, додано GPIO 15 (D2), GPIO 38 (D3).
- В .ino замінено `Arduino_HWSPI` на `Arduino_ESP32QSPI`.
- Перегенеровано wiring_diagram_v3.html.

### 2026-04-26 — v1.1 fixes
- Code review: знайдено 7 критичних, 8 mid, 5+ optimization issues
- Виправлено всі критичні + більшість mid (див. checklist вище)
- Створено PROJECT_NOTES.md, BUILD.md, CHANGELOG.md
- Готово до first hardware test

### 2026-04-26 — перенесено в проект
- Перенесено контекст з попереднього чату (6 частин)
- Файли v1.0 успішно компілювались, але не прошивались на залізо
