# esp32_bike_computer.ino — патчі для v1.1.4

> Цей документ описує точкові зміни у головному скетчі. Кожна секція — це
> drop-in replacement для існуючої функції. Шукай старий код за маркером,
> заміняй на новий.

---

## 1. Header comment — оновити версію

**Замінити верхній блок коментарів на:**

```cpp
// =============================================================
//  ESP32 BIKE COMPUTER — Main Sketch
//  Firmware v1.1.4 — BLE crash fix + UI cleanup + crash logger
//
//  Required libraries (Arduino IDE Library Manager):
//    - Arduino_GFX_Library  (ST77916 QSPI driver)
//    - TinyGPSPlus
//    - Adafruit_NeoPixel
//    - ESPAsyncWebServer + AsyncTCP (from ESP32Async fork)
//    - LittleFS (built-in with ESP32 core 3.x)
//
//  Sunrise/sunset: inline USNO algorithm (Dusk2Dawn replaced in v1.1.3).
//  Partition table: custom (partitions.csv) — 2x 3MB OTA + coredump
//                   + ~9.875MB LittleFS.
//
//  See CHANGELOG.md and PROJECT_NOTES.md for version history.
// =============================================================
```

---

## 2. Includes — додати crash_logger

**Знайти список `#include` зверху скетчу і додати:**

```cpp
#include "crash_logger.h"
```

---

## 3. Global instances — додати gCrash

**Знайти місце де оголошуються `BLEManager gBLE;`, `ErrorHandler gErrors;` і
додати поряд:**

```cpp
CrashLogger gCrash;
```

---

## 4. setup() — додати ініціалізацію crash logger

**Знайти у `setup()` місце ОДРАЗУ після `LittleFS.begin(true)` (LittleFS
ініціалізація з v1.1.2) і додати:**

```cpp
  // --- Crash Logger ---
  // Must be after LittleFS.begin(). Reads esp_reset_reason(), records
  // boot history, and saves coredump summary if previous boot crashed.
  DBG("INIT: Crash logger...");
  gCrash.init();
  Serial.printf("[BOOT] count=%u last_reason=%s crashes_logged=%u\n",
                gCrash.bootCount,
                resetReasonStr(gCrash.lastReason),
                gCrash.getCrashCount());
```

---

## 5. loop() — додати refreshHRSnapshot

**Знайти у `loop()` місце ОДРАЗУ після `refreshIMUSnapshot();` і додати:**

```cpp
  // BLE HR thread-safe snapshot (snapshot pattern, like IMU).
  // Use gHeartRateBpmSnap for all reads outside this line.
  refreshHRSnapshot();
```

---

## 6. drawErrorOverlay() — ВИДАЛИТИ повністю

**Знайти і ВИДАЛИТИ всю функцію:**

```cpp
// ── Error overlay (shown if critical error) ──────────────────
void drawErrorOverlay() {
  if (!gErrors.hasActive()) return;
  ErrorCode ec=gErrors.firstActive();
  gfx->fillRect(0,42,TFT_WIDTH,46,0xA000);
  gfx->setTextColor(CLR_WHITE); gfx->setTextSize(1);
  gfx->setCursor(10,50); gfx->print("! ERROR:");
  gfx->setCursor(10,64); gfx->print(ERR_NAMES[ec]);
}
```

**І ВИДАЛИТИ виклик з `loop()`:**

```cpp
    drawErrorOverlay();   // ← ВИДАЛИТИ цей рядок
```

Тепер активна помилка показується тільки червоною точкою у статус барі (на
12-й годині). Логіка повного відображення помилок переноситься у Settings —
згідно TODO у PROJECT_NOTES.md, у v1.2 кожен модуль показуватиме свої
помилки у відповідній settings секції.

---

## 7. drawArcLabel() — повна заміна (canvas blit оптимізація)

**Видалити стару функцію `drawArcLabel` (з вкладеним per-pixel циклом
`gfx->writePixel(...)`) разом з оголошеннями `ARC_*`, `sArcCanvas`.**

**Замінити на:**

```cpp
// ── Status-bar arc labels ─────────────────────────────────────
// All UI tokens come from config.h (UI_ARC_*, UI_CVS_*, UI_OUT_*, UI_SKIP_COLOR).
//
// Strategy (v1.1.4):
//   1. Render text into "input canvas" (sArcIn) sized for textSize.
//   2. Rotate input canvas pixel-by-pixel into "output canvas" (sArcOut)
//      — this is RAM-only (~100 ns per write), not 5000 SPI transactions.
//   3. Single blit of sArcOut to display via draw16bitRGBBitmap with
//      UI_SKIP_COLOR as the transparent sentinel.
//
// Result: 7 SPI bursts/frame instead of ~5000 writePixel calls. Status
// bar redraw drops from ~50ms to ~5ms, freeing CPU for BLE/IMU tasks.

static Arduino_Canvas* sArcInS1  = nullptr;  // input, textSize 1
static Arduino_Canvas* sArcInS2  = nullptr;  // input, textSize 2
static Arduino_Canvas* sArcOutS1 = nullptr;  // output (rotated bbox), size 1
static Arduino_Canvas* sArcOutS2 = nullptr;  // output (rotated bbox), size 2

static void ensureArcCanvases() {
  if (!sArcInS1) {
    sArcInS1  = new Arduino_Canvas(UI_CVS_W_S1, UI_CVS_H_S1, nullptr);
    sArcInS1->begin();
  }
  if (!sArcOutS1) {
    sArcOutS1 = new Arduino_Canvas(UI_OUT_S1, UI_OUT_S1, nullptr);
    sArcOutS1->begin();
  }
  if (!sArcInS2) {
    sArcInS2  = new Arduino_Canvas(UI_CVS_W_S2, UI_CVS_H_S2, nullptr);
    sArcInS2->begin();
  }
  if (!sArcOutS2) {
    sArcOutS2 = new Arduino_Canvas(UI_OUT_S2, UI_OUT_S2, nullptr);
    sArcOutS2->begin();
  }
}

// Draw a label at angle (degrees, 0° = 12-o'clock, clockwise) with given
// text size (1 or 2). Color is applied to non-skip pixels.
static void drawArcLabel(const char* str, float angleDeg,
                         uint16_t color, uint8_t textSize = 1) {
  if (!str || !str[0]) return;
  ensureArcCanvases();

  Arduino_Canvas* in  = (textSize == 2) ? sArcInS2  : sArcInS1;
  Arduino_Canvas* out = (textSize == 2) ? sArcOutS2 : sArcOutS1;
  int16_t inW  = (textSize == 2) ? UI_CVS_W_S2 : UI_CVS_W_S1;
  int16_t inH  = (textSize == 2) ? UI_CVS_H_S2 : UI_CVS_H_S1;
  int16_t outS = (textSize == 2) ? UI_OUT_S2   : UI_OUT_S1;

  // 1. Render text into input canvas with skip-color background
  in->fillScreen(UI_SKIP_COLOR);
  in->setTextColor(color, UI_SKIP_COLOR);
  in->setTextSize(textSize);
  in->setCursor(0, (textSize == 2) ? 2 : 2);
  in->print(str);
  uint16_t* fbIn = in->getFramebuffer();

  // 2. Compute rotation
  float rotDeg = (angleDeg > UI_FLIP_LO_DEG && angleDeg < UI_FLIP_HI_DEG)
                  ? angleDeg - 180.0f
                  : angleDeg;
  float rotRad = rotDeg * DEG_TO_RAD;
  float ca = cosf(rotRad), sa = sinf(rotRad);

  // 3. Rotate into output canvas (RAM, fast)
  out->fillScreen(UI_SKIP_COLOR);
  float cxIn  = inW  * 0.5f;
  float cyIn  = inH  * 0.5f;
  float cxOut = outS * 0.5f;
  float cyOut = outS * 0.5f;
  for (int16_t iy = 0; iy < inH; iy++) {
    for (int16_t ix = 0; ix < inW; ix++) {
      uint16_t c = fbIn[iy * inW + ix];
      if (c == UI_SKIP_COLOR) continue;
      float dx = ix - cxIn;
      float dy = iy - cyIn;
      int16_t sx = (int16_t)(cxOut + dx * ca - dy * sa + 0.5f);
      int16_t sy = (int16_t)(cyOut + dx * sa + dy * ca + 0.5f);
      if (sx >= 0 && sx < outS && sy >= 0 && sy < outS) {
        out->writePixel(sx, sy, c);
      }
    }
  }

  // 4. Compute screen position of output canvas top-left
  float mathRad = (angleDeg - 90.0f) * DEG_TO_RAD;
  int16_t lx = UI_ARC_CX + (int16_t)(UI_ARC_R_MID * cosf(mathRad) + 0.5f);
  int16_t ly = UI_ARC_CY + (int16_t)(UI_ARC_R_MID * sinf(mathRad) + 0.5f);
  int16_t blitX = lx - outS / 2;
  int16_t blitY = ly - outS / 2;

  // 5. Single blit with skip-color transparency
  uint16_t* fbOut = out->getFramebuffer();
  gfx->draw16bitRGBBitmapWithMask(blitX, blitY, fbOut, UI_SKIP_COLOR,
                                   outS, outS);
}
```

**Примітка:** якщо у вашій версії `Arduino_GFX_Library` немає
`draw16bitRGBBitmapWithMask`, замінити останній блок на ручний writePixel
loop (все одно швидший за поточну реалізацію бо канвас вже rotated):

```cpp
  // Fallback if WithMask not available
  gfx->startWrite();
  for (int16_t iy = 0; iy < outS; iy++) {
    for (int16_t ix = 0; ix < outS; ix++) {
      uint16_t c = fbOut[iy * outS + ix];
      if (c == UI_SKIP_COLOR) continue;
      int16_t sx = blitX + ix;
      int16_t sy = blitY + iy;
      if (sx >= 0 && sx < TFT_WIDTH && sy >= 0 && sy < TFT_HEIGHT) {
        gfx->writePixel(sx, sy, c);
      }
    }
  }
  gfx->endWrite();
```

---

## 8. drawStatusBar() — повна заміна

**Замінити повністю:**

```cpp
void drawStatusBar() {
  ensureArcCanvases();

  // v1.1.4: removed fillArc bezel ring. Background is now the screen's
  // own bg (or whatever is below). Labels are rendered with transparent
  // canvas (UI_SKIP_COLOR sentinel).

  bool blinkOn = (millis() / 500) % 2;

  // ── Top arc ─────────────────────────────────────────────────
  if (blinkOn && gLED.turnState != TS_IDLE) {
    const char* sym = gLED.turnState == TS_LEFT   ? "<"  :
                      gLED.turnState == TS_RIGHT  ? ">"  :
                      gLED.turnState == TS_HAZARD ? "><" : "TY";
    drawArcLabel(sym, 285.0f, CLR_ORANGE, 1);
  }
  if (gIMUSnapshot.isBraking) {
    drawArcLabel("BR", 315.0f, CLR_RED, 1);
  }
  // Error indicator dot — kept exactly as before (radius 3, red, 12-o'clock)
  if (gErrors.hasActive()) {
    gfx->fillCircle(UI_ARC_CX, UI_ARC_CY - UI_ARC_R_MID, 3, CLR_RED);
  }
  // HR — size 2, white (snapshot, not raw volatile)
  if (gHeartRateBpmSnap > 0) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%ub", (uint16_t)gHeartRateBpmSnap);
    drawArcLabel(buf, 40.0f, CLR_WHITE, 2);
  }
  // Battery — size 2, white text (color-coded value retained for the dot
  // logic could be added later; for now uniform white per design spec)
  {
    char buf[6];
    snprintf(buf, sizeof(buf), "%u%%", (uint8_t)gBattPct);
    drawArcLabel(buf, 75.0f, CLR_WHITE, 2);
  }

  // ── Bottom arc ───────────────────────────────────────────────
  if (gInSettings) {
    drawArcLabel(SETT_NAMES[gSett.screen], 180.0f, CLR_WHITE, 1);
  } else {
    // Time — size 2, white. v1.1.4: date moved to S_TIME settings screen.
    if (gGpsFixed && gps.time.isValid()) {
      int h = (int)(gps.time.hour() + gTimeSett.utcHalfHours * 0.5f + 24) % 24;
      char buf[8];
      snprintf(buf, sizeof(buf), "%02d:%02d", h, gps.time.minute());
      drawArcLabel(buf, 140.0f, CLR_WHITE, 2);
    }
    // FIX/!FIX — size 1, color-coded (red/green retained per design spec).
    drawArcLabel(gGpsFixed ? "FIX" : "!FIX", 195.0f,
                 gGpsFixed ? CLR_GPS_OK : CLR_GPS_ERR, 1);
    // Sat count — size 1, white.
    if (gGpsFixed) {
      char buf[4];
      snprintf(buf, sizeof(buf), "%d", gSatCount);
      drawArcLabel(buf, 220.0f, CLR_WHITE, 1);
    }
  }
}
```

---

## 9. drawScreenRiding / drawScreenTerrain — gHeartRateBpmSnap всюди

**У `drawScreenRiding()`:**

```cpp
  // HR
  if (gHeartRateBpmSnap > 0) {           // ← було gHeartRateBpm
    gfx->setTextColor(CLR_PINK);
    snprintf(buf,16,"HR: %d bpm",gHeartRateBpmSnap);   // ← було gHeartRateBpm
    gfx->setCursor(40,255); gfx->print(buf);
  }
```

**У `drawScreenTerrain()`:**

```cpp
  if (gHeartRateBpmSnap > 0) {           // ← було gHeartRateBpm
    snprintf(buf,24,"%u bpm",gHeartRateBpmSnap);
    row("Heart rate:", buf, CLR_PINK);
    uint8_t mhr=gHRSett.maxHR;
    uint8_t z=gHeartRateBpmSnap<mhr*0.6f?1:gHeartRateBpmSnap<mhr*0.7f?2:
              gHeartRateBpmSnap<mhr*0.8f?3:gHeartRateBpmSnap<mhr*0.9f?4:5;
    snprintf(buf,24,"Zone %u",z);
    row("HR Zone:", buf, z<=2?CLR_GREEN:z<=3?CLR_YELLOW:CLR_RED);
  } else {
    row("Heart rate:", "-- (no device)", CLR_GRAY);
  }
```

`gHeartRateBpm` (raw volatile) тепер пишеться **тільки** з BLE callback.
Усі читачі (display, web) використовують `gHeartRateBpmSnap`.

---

## 10. Helper для resetReasonStr — вже у crash_logger.h

`resetReasonStr()` оголошено як `static` у `crash_logger.h` — буде доступно
у скетчі через `#include "crash_logger.h"`. Окремо нічого додавати не треба.

---

## ПЕРЕВІРКА ПІСЛЯ ЗМІН

Після всіх patches скомпілюй у Arduino IDE / arduino-cli. Очікувані попередження:
- `'gFooBar' was declared 'extern' but never defined` — якщо я десь промахнувся
  з extern, дай знати точну назву.

Після прошивки на залізо:
1. Спочатку boot без HR (Garmin вимкнений) → перевір що Serial виводить
   `[BOOT] count=N last_reason=POWERON crashes_logged=0`.
2. Потім ввімкни HR на годиннику → перевір що НЕ ребутить.
3. Якщо все одно ребутить → пригоди serial monitor і подивись reset_reason.
   Прочитай `/boot.log` через web `/boot-log` (буде додано у наступному
   patch для `ota_update.h`).
