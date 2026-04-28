# Patches для drawSettTime та drawSettBattery у `.ino`

> Settings draw functions знаходяться в `.ino` (не в `settings.h`). Шукати
> `void drawSettTime()` і `void drawSettBattery()`.

---

## drawSettTime — додати Date row зверху

**Замінити повністю:**

```cpp
void drawSettTime() {
  char buf[24];

  // v1.1.4: Date row (read-only, moved from status bar bottom-arc)
  if (gGpsFixed && gps.date.isValid()) {
    snprintf(buf, sizeof(buf), "%02d-%02d-%04d",
             gps.date.day(), gps.date.month(), gps.date.year());
  } else {
    snprintf(buf, sizeof(buf), "-- (no GPS fix)");
  }
  // Render at y=50 (top, before active rows that start at 70)
  gfx->setTextSize(1);
  gfx->setTextColor(CLR_GRAY);
  gfx->setCursor(10, 50); gfx->print("Date:");
  gfx->setTextColor(gGpsFixed ? CLR_WHITE : CLR_GRAY);
  gfx->setCursor(180, 50); gfx->print(buf);
  gfx->drawFastHLine(8, 60, TFT_WIDTH - 16, 0x1082);

  // Active editable rows — shift down by one slot (start y=70)
  // drawSettRow uses y = 50 + line*20, so we offset by adding fake +1 line.
  // Simpler: write directly to maintain layout.

  // UTC offset (line 0 → y=70)
  float h = gTimeSett.utcHalfHours * 0.5f;
  if (h >= 0) snprintf(buf, sizeof(buf), "+%.1f", h);
  else        snprintf(buf, sizeof(buf), "%.1f",  h);
  uint8_t y = 70;
  bool active0 = (gSett.activeLine == 0);
  gfx->setTextColor(active0 ? CLR_WHITE : CLR_GRAY);
  gfx->setCursor(10, y); gfx->print("UTC offset:");
  gfx->setTextColor(active0 ? CLR_CYAN : CLR_GRAY);
  gfx->setCursor(180, y); gfx->print(buf);
  if (active0 && (!gSett.isEditing || gSett.ulVisible)) {
    gfx->drawFastHLine(8, y + 9, (int)strlen("UTC offset:") * 6 + 175, CLR_CYAN);
  }

  // DST (line 1 → y=90)
  y = 90;
  bool active1 = (gSett.activeLine == 1);
  gfx->setTextColor(active1 ? CLR_WHITE : CLR_GRAY);
  gfx->setCursor(10, y); gfx->print("DST:");
  gfx->setTextColor(active1 ? CLR_CYAN : CLR_GRAY);
  gfx->setCursor(180, y); gfx->print(gTimeSett.dstEnabled ? "ON" : "OFF");
  if (active1 && (!gSett.isEditing || gSett.ulVisible)) {
    gfx->drawFastHLine(8, y + 9, (int)strlen("DST:") * 6 + 175, CLR_CYAN);
  }

  // Astronomy info (read-only, below editable rows)
  gfx->setTextColor(CLR_YELLOW);
  gfx->setCursor(10, 120); gfx->print("-- ASTRONOMY --");
  if (gGpsFixed && gSunriseH > 0) {
    int srH = (int)gSunriseH, srM = (int)((gSunriseH - srH) * 60);
    int ssH = (int)gSunsetH,  ssM = (int)((gSunsetH  - ssH) * 60);
    snprintf(buf, sizeof(buf), "%02d:%02d", srH, srM);
    gfx->setTextColor(CLR_GRAY);   gfx->setCursor(10, 140); gfx->print("Sunrise:");
    gfx->setTextColor(CLR_YELLOW); gfx->setCursor(180, 140); gfx->print(buf);
    snprintf(buf, sizeof(buf), "%02d:%02d", ssH, ssM);
    gfx->setTextColor(CLR_GRAY);   gfx->setCursor(10, 160); gfx->print("Sunset:");
    gfx->setTextColor(CLR_ORANGE); gfx->setCursor(180, 160); gfx->print(buf);
  } else {
    gfx->setTextColor(CLR_GRAY);
    gfx->setCursor(10, 140); gfx->print("Sunrise/Sunset: no fix");
  }
}
```

> **Примітка:** `SETT_LINES[S_TIME] = 2` — без змін. Date — read-only,
> не входить у активні рядки. Енкодер як і раніше переходить між UTC offset
> (line 0) і DST (line 1).

---

## drawSettBattery — додати crash count внизу

**Знайти `drawSettBattery()` і додати в кінець функції:**

```cpp
  // v1.1.4: Crash log status row — at the bottom of S_BATTERY screen
  extern CrashLogger gCrash;
  uint8_t y = TFT_HEIGHT - 60;  // adjust if conflicts with existing layout
  gfx->setTextSize(1);
  gfx->setTextColor(CLR_GRAY);
  gfx->setCursor(10, y); gfx->print("-- CRASH LOG --");
  y += 14;

  char buf[32];
  snprintf(buf, sizeof(buf), "%s",
           resetReasonStr(gCrash.lastReason));
  gfx->setTextColor(CLR_GRAY);     gfx->setCursor(10, y);  gfx->print("Last reset:");
  gfx->setTextColor(gCrash.wasAbnormal ? CLR_RED : CLR_GREEN);
  gfx->setCursor(180, y); gfx->print(buf);
  y += 18;

  snprintf(buf, sizeof(buf), "%u", gCrash.bootCount);
  gfx->setTextColor(CLR_GRAY); gfx->setCursor(10, y);  gfx->print("Boot count:");
  gfx->setTextColor(CLR_WHITE); gfx->setCursor(180, y); gfx->print(buf);
  y += 18;

  snprintf(buf, sizeof(buf), "%u", gCrash.getCrashCount());
  gfx->setTextColor(CLR_GRAY); gfx->setCursor(10, y);  gfx->print("Saved crashes:");
  gfx->setTextColor(gCrash.getCrashCount() > 0 ? CLR_ORANGE : CLR_GREEN);
  gfx->setCursor(180, y); gfx->print(buf);
```

Якщо macет дисплея цей блок не вмещає — можна показувати тільки одну summary-лінію
"Last: PANIC ×3" (де 3 = crashCount). Скажи якщо треба компактніше.
