// =============================================================
//  ESP32 BIKE COMPUTER — Main Sketch v1.1.3
//  Board: ESP32-S3-N16R8
//  Arduino IDE board: "ESP32S3 Dev Module"
//
//  Required libraries (install via Library Manager or git):
//    • Arduino_GFX_Library (moononournation)  — Display
//    • TinyGPSPlus (mikalhart)                — GPS parsing
//    • Adafruit_NeoPixel                      — WS2812B LEDs (replaces FastLED — no pin-validation issues)
//    • MPU6050 (or use included mpu6500.h)    — IMU I2C
//    • ESPAsyncWebServer (me-no-dev)          — OTA web server
//    • AsyncTCP (me-no-dev)                   — needed by above
//    • LittleFS (built-in with ESP32 core 3.x) — file system
//    (Dusk2Dawn removed in v1.1.3 — sunrise/sunset computed inline, USNO algorithm)
//
//  Board Settings:
//    Flash: 16MB (128Mb), QIO 80MHz
//    PSRAM: OPI PSRAM
//    Partition: Custom (uses partitions.csv from sketch folder)
//      → 2× 3MB OTA slots + 64KB coredump + ~9.875MB LittleFS
//    Upload speed: 921600
//
//  NRF52840 rear unit: ALL CALLS COMMENTED OUT
// =============================================================

#include "config.h"
#include "ui_theme.h"
#include "error_handler.h"
#include "settings.h"
#include "ble_manager.h"
#include "mpu6500.h"
#include "led_controller.h"
#include "ota_update.h"

#include <Arduino_GFX_Library.h>
#include <TinyGPS++.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <LittleFS.h>

// =============================================================
//  GLOBAL INSTANCES (extern declared in headers)
// =============================================================
UITheme       gTheme;
ErrorHandler  gErrors;
BLEManager    gBLE;
MPU6500Driver gIMU;
LEDController gLED;
OtaManager    gOTA;
Preferences   gPrefs;

SettingsState gSett;
LedSettings   gLedSett;
TimeSettings  gTimeSett;
HRSettings    gHRSett;

// =============================================================
//  FORWARD DECLARATIONS — variables defined later in the file
//  (needed because Arduino .ino compiles top-to-bottom)
// =============================================================
extern uint8_t  gMapMode;    // defined below drawStatusBar
extern float    gMapOffX, gMapOffY, gMapScale; // defined below drawStatusBar
extern uint32_t gLastGpsMs;  // defined below processGPS
void _calcSlope(float distM, float altM);  // defined below processGPS
void settingsEncPress();  // defined below handleEncoder

// =============================================================
//  GPS
// =============================================================
TinyGPSPlus gps;
HardwareSerial gpsSerial(GPS_UART_NUM);

// Exposed GPS state (used by OTA web pages)
float  gLat=0, gLon=0, gAltM=0, gSpeedKmh=0, gCourseD=0;
float  gBattVolt=0, gBattPct=0;
float  gTotalDistKm=0, gSessionDistKm=0;
int    gSatCount=0;
bool   gGpsFixed=false;
double gPrevLat=0, gPrevLon=0;
bool   gHasPrevPos=false;
float  gLastSavedKm=0;

// GPS track (allocated in PSRAM)
struct TrackPoint { float lat,lon,speedKmh; };
TrackPoint* gTrack       = nullptr;
uint16_t    gTrackLen    = 0;
uint32_t    gLastTrackMs = 0;
float       gMinLat=1e6,gMaxLat=-1e6,gMinLon=1e6,gMaxLon=-1e6;
float       gOriginLat=0,gOriginLon=0;

// Slope
float gSlopePercent = 0;
static float _slopeDistAcc=0, _slopeAltStart=0;
bool  _slopeInit=false;

// Session timing
uint32_t gSessionStartMs = 0;
float    gMaxSpeed=0, gAvgSpeedAcc=0;
uint32_t gAvgSpeedN=0;

// Astronomy
float gSunriseH=0, gSunsetH=0;

// =============================================================
//  DISPLAY — QSPI 4-line bus
// =============================================================
// ST77916 on this module uses QSPI (4 parallel data lines), NOT regular SPI.
// Module pinout: SDA=D0, SCL=SCK, IO1=D1, IO2=D2, IO3=D3 (no DC pin).
// Arduino_ESP32QSPI(cs, sck, d0, d1, d2, d3) — order matters.
Arduino_DataBus *tftBus = new Arduino_ESP32QSPI(
  PIN_TFT_CS, PIN_TFT_SCK,
  PIN_TFT_D0, PIN_TFT_D1, PIN_TFT_D2, PIN_TFT_D3);
// ST77916 driver — module ver:tft 1.53 uses 150-init (not default 180-init).
// ips=true for IPS color inversion; flip to false if colors appear inverted.
Arduino_GFX *gfx = new Arduino_ST77916(
  tftBus, PIN_TFT_RST, TFT_ROTATION, true,
  ST77916_TFTWIDTH, ST77916_TFTHEIGHT, 0, 0, 0, 0,
  st77916_150_init_operations, sizeof(st77916_150_init_operations));

// Screens
uint8_t  gScreen      = SCR_RIDING;
bool     gInSettings  = false;
uint32_t gLastScrollMs=0;   // for auto-scroll between main screens
uint32_t gLastDrawMs=0;     // for full-screen redraw throttling
uint32_t gLastSbarMs=0;     // for status-bar redraw throttling
bool     gDisplayDirty=true;

// Calibration request flag — set from web/UI, executed in loop()
// to avoid Preferences race between IMU task and main thread.
volatile bool gCalibRequested = false;

// =============================================================
//  TE (Tearing Effect) — ISR-driven FPS counter (v1.1.2)
// =============================================================
// ST77916 pulses TE pin once per internal frame refresh (~60 Hz).
// We count pulses here for a real-time FPS metric — useful for tuning
// QSPI clock and validating partial-redraw work in v1.2.
volatile uint32_t gTeTickCount = 0;
volatile bool     gTEReady = false;
uint32_t gTeFpsLastMs = 0;
uint32_t gTeFpsLastCount = 0;
uint8_t  gDisplayFps = 0;

void IRAM_ATTR teISR() {
  gTeTickCount += 1;
  gTEReady = true;
}

// Block until the next TE pulse (display scan complete), then draw immediately.
// Timeout 25ms prevents lockup if TE pin is unwired.
static void waitForTE() {
  gTEReady = false;
  uint32_t t = millis();
  while (!gTEReady && millis() - t < 25) {}
}

// =============================================================
//  ENCODER — ISR
// =============================================================
volatile int32_t gEncPos = 0;
static uint8_t   _encAB  = 0;
static const int8_t ENC_TBL[16]={0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};

void IRAM_ATTR encoderISR() {
  _encAB = (_encAB<<2)&0x0F;
  _encAB |= (digitalRead(PIN_ENC_DT)<<1)|digitalRead(PIN_ENC_CLK);
  gEncPos += ENC_TBL[_encAB];
}

// Encoder state machine
int32_t  _encLast    = 0;
bool     _swDown     = false;
uint32_t _swDownMs   = 0;
bool     _swLongFired= false;

// =============================================================
//  BUTTONS (external modules, HIGH = pressed)
// =============================================================
bool     _b1Prev=false,_b2Prev=false,_b3Prev=false;
uint32_t _b3DownMs=0; bool _b3Down=false;

bool btnPressed(uint8_t pin) { return digitalRead(pin) == HIGH; }

void handleButtons() {
  bool b1=btnPressed(PIN_BTN1), b2=btnPressed(PIN_BTN2), b3=btnPressed(PIN_BTN3);
  uint32_t now=millis();

  // BTN1 — Left turn
  if (b1 && !_b1Prev) {
    if (gLED.turnState==TS_LEFT) gLED.setIdle();
    else { gLED.setLeft(); DBG("BTN1: LEFT turn"); }
  }
  // BTN2 — Right turn
  if (b2 && !_b2Prev) {
    if (gLED.turnState==TS_RIGHT) gLED.setIdle();
    else { gLED.setRight(); DBG("BTN2: RIGHT turn"); }
  }
  // BTN3 — ThankYou / Hazard (long press)
  if (b3 && !_b3Down) { _b3Down=true; _b3DownMs=now; }
  if (!b3 && _b3Down) {
    uint32_t dur=now-_b3DownMs; _b3Down=false;
    if (dur < BTN_LONG_MS) {
      gLED.setThankYou(); DBG("BTN3: THANK YOU");
    } else {
      if (gLED.turnState==TS_HAZARD) gLED.setIdle();
      else { gLED.setHazard(); DBG("BTN3 LONG: HAZARD"); }
    }
  }
  _b1Prev=b1; _b2Prev=b2; _b3Prev=b3;
}

// =============================================================
//  ENCODER LOGIC
// =============================================================
void handleEncoder() {
  int32_t pos=gEncPos;
  int32_t delta=pos-_encLast;
  _encLast=pos;
  uint32_t now=millis();

  if (delta!=0) {
    int8_t dir=(delta>0)?1:-1;
    if (gInSettings) {
      settRotate(gSett,dir,gTheme,gLedSett,gTimeSett,gHRSett,gPrefs);
      gDisplayDirty=true;
    } else if (gScreen==SCR_MAP && gMapMode>0) {
      // Manual MAP modes: encoder controls scale/pan, NOT screen change
      switch(gMapMode) {
        case 1: // zoom
          gMapScale *= (dir>0) ? 1.2f : 0.833f;
          gMapScale = constrain(gMapScale, 0.1f, 100.0f);
          break;
        case 2: // pan horizontal
          gMapOffX += dir * 20.0f;
          break;
        case 3: // pan vertical
          gMapOffY += dir * 20.0f;
          break;
      }
      gDisplayDirty=true;
    } else {
      // Cycle main screens
      int8_t s=(int8_t)gScreen+dir;
      gScreen=(uint8_t)((s+SCR_COUNT)%SCR_COUNT);
      gDisplayDirty=true;
      gLastScrollMs=now;
    }
  }

  bool swNow=(digitalRead(PIN_ENC_SW)==LOW);
  if (swNow && !_swDown) { _swDown=true; _swDownMs=now; _swLongFired=false; }
  if (_swDown && !swNow) {
    uint32_t dur=now-_swDownMs; _swDown=false;
    if (dur < ENC_DEB_MS) return;
    if (gInSettings) {
      if (dur < BTN_SHORT_MS) {
        settingsEncPress();
      }
    } else {
      // short press: context action on current screen
      if (dur < BTN_SHORT_MS && gScreen==SCR_MAP) {
        gMapMode=(gMapMode+1)%4;
        // Returning to auto mode: reset manual offsets and scale
        if (gMapMode==0) {
          gMapOffX=0; gMapOffY=0; gMapScale=1.0f;
        }
        gDisplayDirty=true;
        DBG("MAP mode: %d", gMapMode);
      }
    }
  }
  // Long press → settings
  if (_swDown && !_swLongFired && (now-_swDownMs)>=BTN_SETT_MS) {
    _swLongFired=true;
    bool wasInSettings = gInSettings;
    gInSettings=!gInSettings;
    gDisplayDirty=true;
    DBG("Settings: %s", gInSettings?"OPEN":"CLOSE");
    // v1.1.2: Force-save odometer when entering settings menu.
    // Settings are typically opened during stops, so this is a natural
    // checkpoint with low write pressure on NVS.
    if (gInSettings && !wasInSettings) {
      extern float gTotalDistKm;
      extern float gLastSavedKm;
      gPrefs.begin(PREF_BIKE, false);
      gPrefs.putFloat(PREF_TOTAL, gTotalDistKm);
      gPrefs.end();
      gLastSavedKm = gTotalDistKm;
      DBG("Settings open: odometer saved at %.2f km", gTotalDistKm);
    }
  }
}

void settingsEncPress() {
  if (gSett.screen==S_OTA && gSett.activeLine==0) {
    if (gOTA.active) gOTA.stop(); else gOTA.start();
    return;
  }
  toggleEdit(gSett);
  gDisplayDirty=true;
}

// =============================================================
//  GPS PROCESSING
// =============================================================
void processGPS(uint32_t now) {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
  if (now-gLastGpsMs < GPS_UPDATE_MS) return;
  gLastGpsMs=now;

  // GPS error check
  if (gps.charsProcessed()<10 && now>5000) gErrors.set(ERR_GPS, SEV_WARN);
  else if (gps.charsProcessed()>10) gErrors.clear(ERR_GPS);

  gSatCount = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;
  gGpsFixed = gps.location.isValid() && gSatCount>=GPS_MIN_SATS;
  gSpeedKmh = gps.speed.isValid() ? gps.speed.kmph() : 0;
  gCourseD  = gps.course.isValid() ? gps.course.deg() : 0;
  gAltM     = gps.altitude.isValid() ? gps.altitude.meters() : 0;

  if (!gGpsFixed) return;
  gLat = gps.location.lat();
  gLon = gps.location.lng();

  // Session stats — bounded running average to prevent float overflow
  if (gSpeedKmh>gMaxSpeed) gMaxSpeed=gSpeedKmh;
  if (gSpeedKmh>0) {
    gAvgSpeedAcc+=gSpeedKmh; gAvgSpeedN++;
    // Reset accumulator when N gets large to keep numerical precision
    // (16-bit-ish window: average resets every ~32k samples ≈ 100min @ 5Hz)
    if (gAvgSpeedN > 32000) {
      gAvgSpeedAcc /= 2; gAvgSpeedN /= 2;
    }
  }

  // Distance accumulation
  if (gHasPrevPos) {
    double dKm=TinyGPSPlus::distanceBetween(gPrevLat,gPrevLon,gLat,gLon)/1000.0;
    if (dKm < GPS_MAX_JUMP_KM) {
      gSessionDistKm+=(float)dKm;
      gTotalDistKm  +=(float)dKm;
      // Slope
      _calcSlope((float)dKm*1000, gAltM);
      // Save odometer
      if (gTotalDistKm-gLastSavedKm >= ODOM_SAVE_KM) {
        gPrefs.begin(PREF_BIKE,false);
        gPrefs.putFloat(PREF_TOTAL,gTotalDistKm);
        gPrefs.end();
        gLastSavedKm=gTotalDistKm;
      }
    }
  } else { _slopeAltStart=gAltM; _slopeInit=true; gOriginLat=gLat; gOriginLon=gLon; }
  gPrevLat=gLat; gPrevLon=gLon; gHasPrevPos=true;

  // Track recording
  float distFromLast=gTrackLen>0 ?
    (float)TinyGPSPlus::distanceBetween(
      gTrack[gTrackLen-1].lat,gTrack[gTrackLen-1].lon,gLat,gLon) : TRACK_MIN_M+1;
  if (distFromLast>=TRACK_MIN_M && gTrackLen<TRACK_MAX_PTS && gTrack) {
    gTrack[gTrackLen++]={gLat,gLon,gSpeedKmh};
    // Update bounding box
    gMinLat=min(gMinLat,gLat); gMaxLat=max(gMaxLat,gLat);
    gMinLon=min(gMinLon,gLon); gMaxLon=max(gMaxLon,gLon);
  }
}

void _calcSlope(float distM, float altM) {
  _slopeDistAcc+=distM;
  if (_slopeDistAcc >= GPS_SLOPE_WIN_M) {
    float dAlt=altM-_slopeAltStart;
    gSlopePercent=dAlt/_slopeDistAcc*100.0f;
    _slopeAltStart=altM; _slopeDistAcc=0;
  }
}

uint32_t gLastGpsMs=0;

// =============================================================
//  BATTERY ADC
// =============================================================
uint32_t gLastBatMs=0;
void readBattery(uint32_t now) {
  if (now-gLastBatMs < 1000) return;
  gLastBatMs=now;
  uint32_t sum=0;
  for (int i=0;i<VBAT_SAMPLES;i++) { sum+=analogReadMilliVolts(PIN_VBAT); delay(1); }
  float vAdc=(float)(sum/VBAT_SAMPLES)/1000.0f;
  gBattVolt=vAdc*VBAT_RATIO;
  gBattPct=constrain((gBattVolt-VBAT_EMPTY)/(VBAT_FULL-VBAT_EMPTY)*100.0f,0,100);
}

// =============================================================
//  IMU TASK (Core 0)
// =============================================================
TaskHandle_t hIMUTask=nullptr;
portMUX_TYPE gIMUMux = portMUX_INITIALIZER_UNLOCKED;

// Snapshot of IMU data for thread-safe reads (loop, drawing, web).
// Refreshed once per loop iteration in the main thread.
IMUData gIMUSnapshot = {};

void imuTask(void *pv) {
  TickType_t xLastWake=xTaskGetTickCount();
  const TickType_t period=pdMS_TO_TICKS(1000/IMU_SAMPLE_HZ);
  while(1) {
    // Sample writes to gIMU.data. We snapshot under spinlock below
    // to avoid reading torn floats from another core.
    portENTER_CRITICAL(&gIMUMux);
    gIMU.sample(gSpeedKmh);
    portEXIT_CRITICAL(&gIMUMux);
    // Send brake to NRF (COMMENTED OUT):
    // uint8_t brk=(uint8_t)(gIMUSnapshot.brakeIntensity*100);
    // gBLE.sendBrakeCmd(brk);
    vTaskDelayUntil(&xLastWake, period);
  }
}

// Safely copy IMU state for use in main thread / drawing / web
inline void refreshIMUSnapshot() {
  portENTER_CRITICAL(&gIMUMux);
  gIMUSnapshot = gIMU.data;
  portEXIT_CRITICAL(&gIMUMux);
}

// =============================================================
//  ASTRONOMY — USNO sunrise/sunset algorithm (v1.1.3)
//  Replaces Dusk2Dawn library. Source: edwilliams.org/sunrise_sunset_algorithm.htm
//  Accuracy: ±1 min for latitudes within ±72°. Returns minutes from local
//  midnight, or -1 for polar day/night (sun never rises/sets).
// =============================================================
static int calcSunMinutes(int year, int month, int day,
                          float lat, float lon,
                          float utcOffset, bool dst, bool isSunrise) {
  // Day of year (N)
  float N1 = floorf(275.0f * month / 9.0f);
  float N2 = floorf((month + 9.0f) / 12.0f);
  float N3 = 1.0f + floorf((year - 4.0f * floorf(year / 4.0f) + 2.0f) / 3.0f);
  float N  = N1 - (N2 * N3) + day - 30.0f;

  // Longitude hour value and approximate time
  float lngHour = lon / 15.0f;
  float t = isSunrise ? N + (6.0f  - lngHour) / 24.0f
                      : N + (18.0f - lngHour) / 24.0f;

  // Sun's mean anomaly
  float M = (0.9856f * t) - 3.289f;

  // Sun's true longitude (L), clamped to [0, 360)
  float L = M + (1.916f * sinf(M * DEG_TO_RAD))
              + (0.020f * sinf(2.0f * M * DEG_TO_RAD))
              + 282.634f;
  L = fmodf(L, 360.0f);
  if (L < 0) L += 360.0f;

  // Sun's right ascension (RA), adjusted to same quadrant as L
  float RA = atanf(0.91764f * tanf(L * DEG_TO_RAD)) * RAD_TO_DEG;
  RA = fmodf(RA, 360.0f);
  if (RA < 0) RA += 360.0f;
  float Lq = floorf(L  / 90.0f) * 90.0f;
  float Rq = floorf(RA / 90.0f) * 90.0f;
  RA = (RA + (Lq - Rq)) / 15.0f;

  // Sun's declination
  float sinDec = 0.39782f * sinf(L * DEG_TO_RAD);
  float cosDec = cosf(asinf(sinDec));

  // Sun's local hour angle — zenith 90°50' (official sunrise/sunset)
  const float cosZenith = cosf(90.833f * DEG_TO_RAD);
  float cosH = (cosZenith - sinDec * sinf(lat * DEG_TO_RAD))
               / (cosDec  * cosf(lat * DEG_TO_RAD));

  // Polar day / polar night
  if (cosH >  1.0f) return -1;  // sun never rises
  if (cosH < -1.0f) return -1;  // sun never sets

  float H = isSunrise ? 360.0f - acosf(cosH) * RAD_TO_DEG
                      :           acosf(cosH) * RAD_TO_DEG;
  H /= 15.0f;

  // Local mean time of rise/set
  float T  = H + RA - (0.06571f * t) - 6.622f;
  float UT = fmodf(T - lngHour, 24.0f);
  if (UT < 0) UT += 24.0f;

  // Convert UTC → local
  float local = fmodf(UT + utcOffset + (dst ? 1.0f : 0.0f), 24.0f);
  if (local < 0) local += 24.0f;

  return (int)(local * 60.0f);
}

uint32_t gLastAstroMs = 0;
void updateAstronomy(uint32_t now) {
  if (!gGpsFixed) return;
  if (now - gLastAstroMs < 3600000UL) return;
  gLastAstroMs = now;
  float utcOff = gTimeSett.utcHalfHours * 0.5f;
  int sr = calcSunMinutes(gps.date.year(), gps.date.month(), gps.date.day(),
                          gLat, gLon, utcOff, gTimeSett.dstEnabled, true);
  int ss = calcSunMinutes(gps.date.year(), gps.date.month(), gps.date.day(),
                          gLat, gLon, utcOff, gTimeSett.dstEnabled, false);
  gSunriseH = (sr >= 0) ? sr / 60.0f : 0.0f;
  gSunsetH  = (ss >= 0) ? ss / 60.0f : 0.0f;
  DBG("Astro: rise=%.2f set=%.2f", gSunriseH, gSunsetH);
}

// =============================================================
//  DISPLAY HELPERS
// =============================================================
// Map mode
uint8_t gMapMode=0; // 0=auto,1=zoom,2=pan-h,3=pan-v
float   gMapOffX=0,gMapOffY=0,gMapScale=1.0f;

void drawStatusBar() {
  uint16_t bg  = PAL565[gTheme.bg_statusbar];
  uint16_t fg  = statusbarTextColor();
  gfx->fillRect(0,0,TFT_WIDTH,22,bg);
  gfx->setTextSize(1); gfx->setTextColor(fg);

  // Time (GPS)
  if (gGpsFixed && gps.time.isValid()) {
    int h=gps.time.hour(), m=gps.time.minute();
    float utcOff=gTimeSett.utcHalfHours*0.5f;
    h=(int)(h+utcOff+24)%24;
    char t[6]; snprintf(t,6,"%02d:%02d",h,m);
    gfx->setCursor(4,7); gfx->print(t);
    // Date
    static const char* DOW[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    // simple DOW from TinyGPS (not available directly — use stored)
    char d[12]; snprintf(d,12,"%02d-%02d",gps.date.day(),gps.date.month());
    gfx->setCursor(50,7); gfx->print(d);
  }

  // Turn indicators (blink 2Hz)
  bool blinkOn=((millis()/500)%2==0);
  uint8_t tx=115;
  if (blinkOn) {
    switch(gLED.turnState) {
      case TS_LEFT:     gfx->setTextColor(CLR_ORANGE); gfx->setCursor(tx,7); gfx->print('<'); break;
      case TS_RIGHT:    gfx->setTextColor(CLR_ORANGE); gfx->setCursor(tx,7); gfx->print('>'); break;
      case TS_HAZARD:   gfx->setTextColor(CLR_ORANGE); gfx->setCursor(tx,7); gfx->print("><"); break;
      case TS_THANKYOU: gfx->setTextColor(CLR_ORANGE); gfx->setCursor(tx,7); gfx->print("TY"); break;
      default: break;
    }
  }

  // Brake
  if (gIMUSnapshot.isBraking) {
    gfx->setTextColor(CLR_RED); gfx->setCursor(133,7); gfx->print("BR");
  }

  // HR
  if (gHeartRateBpm>0) {
    gfx->setTextColor(CLR_PINK); gfx->setCursor(150,7);
    gfx->printf("%dbpm",gHeartRateBpm);
  }

  // GPS fix
  gfx->setCursor(200,7);
  gfx->setTextColor(gGpsFixed?CLR_GPS_OK:CLR_GPS_ERR);
  gfx->print(gGpsFixed?"FIX":"!FIX");
  if (gGpsFixed) {
    gfx->setTextColor(CLR_YELLOW); gfx->setCursor(225,7);
    gfx->print(gSatCount);
  }

  // BLE (NRF disabled) — show Garmin HR status
  gfx->setCursor(238,7);
  gfx->setTextColor(gBLE.hrConnected()?CLR_BLUE:CLR_GRAY);
  gfx->print(gBLE.hrConnected()?"HR":"  ");

  // Battery
  uint8_t bpct=(uint8_t)gBattPct;
  uint16_t bclr=battColor565(bpct);
  gfx->setTextColor(bclr);
  gfx->setCursor(255,7);
  gfx->printf("%d%%",bpct);

  // Error dot
  if (gErrors.hasActive()) {
    gfx->fillCircle(TFT_WIDTH-6,6,4,CLR_RED);
  }

  // Settings page name at bottom of status ring
  if (gInSettings) {
    gfx->setTextColor(fg); gfx->setTextSize(1);
    const char *pn=SETT_NAMES[gSett.screen];
    int16_t px=(TFT_WIDTH-strlen(pn)*6)/2;
    gfx->setCursor(px,TFT_HEIGHT-14);
    gfx->fillRect(0,TFT_HEIGHT-18,TFT_WIDTH,18,bg);
    gfx->print(pn);
  }
}

// ── Error overlay (shown if critical error) ──────────────────
void drawErrorOverlay() {
  if (!gErrors.hasActive()) return;
  ErrorCode ec=gErrors.firstActive();
  gfx->fillRect(0,30,TFT_WIDTH,50,0xA000);
  gfx->setTextColor(CLR_WHITE); gfx->setTextSize(1);
  gfx->setCursor(10,40); gfx->print("! ERROR:");
  gfx->setCursor(10,54); gfx->print(ERR_NAMES[ec]);
}

// ── Screen 1: RIDING DATA ─────────────────────────────────────
void drawScreenRiding() {
  uint16_t bg=PAL565[gTheme.bg_screen];
  gfx->fillRect(0,22,TFT_WIDTH,TFT_HEIGHT-22,bg);

  // Big speed
  char buf[16];
  snprintf(buf,16,"%.1f",gSpeedKmh);
  gfx->setTextColor(CLR_WHITE); gfx->setTextSize(7);
  gfx->setCursor((TFT_WIDTH-(int)strlen(buf)*42)/2, 60);
  gfx->print(buf);

  gfx->setTextColor(CLR_GRAY); gfx->setTextSize(2);
  gfx->setCursor(150,150); gfx->print("km/h");

  gfx->drawFastHLine(50,170,260,CLR_GRAY);

  // Data rows
  gfx->setTextSize(2); gfx->setTextColor(CLR_CYAN);
  snprintf(buf,16,"Dist: %.2f km",gSessionDistKm);
  gfx->setCursor(40,180); gfx->print(buf);

  // Cadence — NRF disabled
  gfx->setTextColor(CLR_GRAY); // gCadenceRpm == 0
  snprintf(buf,16,"Cad: -- rpm"); // NRF disabled
  gfx->setCursor(40,205); gfx->print(buf);

  // Heading
  if (gGpsFixed && gCourseD>0) {
    const char* compass[]={"N","NE","E","SE","S","SW","W","NW"};
    int ci=(int)((gCourseD+22.5)/45)%8;
    gfx->setTextColor(CLR_YELLOW);
    snprintf(buf,16,"%.0f %s",gCourseD,compass[ci]);
    gfx->setCursor(40,230); gfx->print(buf);
  }

  // HR
  if (gHeartRateBpm>0) {
    gfx->setTextColor(CLR_PINK);
    snprintf(buf,16,"HR: %d bpm",gHeartRateBpm);
    gfx->setCursor(40,255); gfx->print(buf);
  }

  // Session time
  uint32_t elapsed=(millis()-gSessionStartMs)/1000;
  snprintf(buf,16,"%02lu:%02lu:%02lu",elapsed/3600,(elapsed%3600)/60,elapsed%60);
  gfx->setTextColor(CLR_GRAY); gfx->setTextSize(2);
  gfx->setCursor(110,290); gfx->print(buf);
}

// ── Screen 2: GPS MAP ─────────────────────────────────────────
void drawScreenMap() {
  uint16_t bg=PAL565[gTheme.bg_screen];
  gfx->fillRect(0,22,TFT_WIDTH,TFT_HEIGHT-22,bg);

  if (gTrackLen < 2) {
    gfx->setTextColor(CLR_GRAY); gfx->setTextSize(2);
    gfx->setCursor(80,160); gfx->print("Acquiring GPS...");
    return;
  }

  // Auto-scale (only in mode 0); manual modes use gMapScale as multiplier
  float rangeLatM=(gMaxLat-gMinLat)*111320.0f;
  float rangeLonM=(gMaxLon-gMinLon)*111320.0f*cos(gOriginLat*DEG_TO_RAD);
  float range=max(rangeLatM,rangeLonM)+50;
  float mapArea=280.0f;
  float autoScale=range/mapArea; // m per pixel
  if (autoScale<0.1f) autoScale=0.1f;
  // In manual zoom mode, gMapScale acts as zoom multiplier
  // (>1 = zoomed in / smaller m-per-pixel; <1 = zoomed out)
  float scale = (gMapMode==0) ? autoScale : (autoScale / gMapScale);

  float cx=TFT_WIDTH/2.0f, cy=(22+TFT_HEIGHT)/2.0f;
  auto toScrX=[&](float lon)->int16_t {
    return (int16_t)(cx + (lon-gOriginLon)*111320.0f*cos(gOriginLat*DEG_TO_RAD)/scale + gMapOffX);
  };
  auto toScrY=[&](float lat)->int16_t {
    return (int16_t)(cy - (lat-gOriginLat)*111320.0f/scale + gMapOffY);
  };

  // Draw track with speed color coding
  uint16_t trkClr=PAL565[gTheme.track_line];
  for (uint16_t i=1;i<gTrackLen;i++) {
    float spd=gTrack[i].speedKmh;
    uint16_t lc;
    if      (spd<10)  lc=PAL565[COL_CYAN];
    else if (spd<25)  lc=PAL565[COL_GREEN];
    else if (spd<40)  lc=PAL565[COL_YELLOW];
    else              lc=PAL565[COL_RED];
    gfx->drawLine(toScrX(gTrack[i-1].lon),toScrY(gTrack[i-1].lat),
                  toScrX(gTrack[i].lon),  toScrY(gTrack[i].lat), lc);
  }

  // Current position dot
  if (gGpsFixed) {
    int16_t px=toScrX(gLon), py=toScrY(gLat);
    gfx->fillCircle(px,py,5,CLR_BLUE);
    gfx->drawCircle(px,py,7,CLR_WHITE);
  }

  // Scale bar
  float scaleBarM=1000.0f;
  if (scale>5)  scaleBarM=5000;
  if (scale>25) scaleBarM=10000;
  if (scale<1)  scaleBarM=500;
  if (scale<0.5f) scaleBarM=200;
  int16_t barPx=(int16_t)(scaleBarM/scale);
  uint16_t sclr=PAL565[gTheme.scale_bar];
  gfx->drawFastHLine(10,TFT_HEIGHT-18,barPx,sclr);
  gfx->drawFastVLine(10,TFT_HEIGHT-22,8,sclr);
  gfx->drawFastVLine(10+barPx,TFT_HEIGHT-22,8,sclr);
  gfx->setTextColor(sclr); gfx->setTextSize(1);
  char scBuf[12];
  if (scaleBarM>=1000) snprintf(scBuf,12,"%.0f km",scaleBarM/1000);
  else                  snprintf(scBuf,12,"%.0f m",scaleBarM);
  gfx->setCursor(12,TFT_HEIGHT-12); gfx->print(scBuf);

  // Mode indicator
  const char* modes[]={"","[+ -]","[<  >]","[^ v]"};
  if (gMapMode>0) {
    gfx->setTextColor(sclr); gfx->setTextSize(1);
    gfx->setCursor(TFT_WIDTH-50,28); gfx->print(modes[gMapMode]);
  }
}

// ── Screen 3: TERRAIN & BODY ──────────────────────────────────
void drawScreenTerrain() {
  uint16_t bg=PAL565[gTheme.bg_screen];
  gfx->fillRect(0,22,TFT_WIDTH,TFT_HEIGHT-22,bg);

  gfx->setTextSize(1);
  uint8_t y=30;
  auto row=[&](const char* lbl,const char* val,uint16_t vc=CLR_WHITE){
    gfx->setTextColor(CLR_GRAY); gfx->setCursor(10,y); gfx->print(lbl);
    gfx->setTextColor(vc);       gfx->setCursor(170,y); gfx->print(val);
    gfx->drawFastHLine(8,y+10,TFT_WIDTH-16,0x1082);
    y+=18;
  };
  char buf[24];

  // Slope section
  gfx->setTextColor(CLR_CYAN); gfx->setCursor(10,y); gfx->print("-- SLOPE & TILT --"); y+=14;
  snprintf(buf,24,"%.1f %%",gSlopePercent);
  row("GPS Slope:", buf, gSlopePercent>0?CLR_RED:CLR_CYAN);
  snprintf(buf,24,"%.1f deg",gIMUSnapshot.pitch);
  row("Tilt (IMU):", buf);
  snprintf(buf,24,"%.1f deg",gIMUSnapshot.roll);
  row("Lean angle:", buf);

  // Road quality
  gfx->setTextColor(CLR_CYAN); gfx->setCursor(10,y); gfx->print("-- ROAD QUALITY --"); y+=14;
  snprintf(buf,24,"%u bumps",gIMUSnapshot.potholeCount);
  row("Potholes:", buf, CLR_ORANGE);
  const char* vibStr=gIMUSnapshot.vibrationRMS<2?"Low":gIMUSnapshot.vibrationRMS<5?"Moderate":"High";
  row("Vibration:", vibStr, gIMUSnapshot.vibrationRMS<2?CLR_GREEN:gIMUSnapshot.vibrationRMS<5?CLR_YELLOW:CLR_RED);
  snprintf(buf,24,"%.1f/10",gIMUSnapshot.vibrationRMS);
  row("Vib score:", buf);

  // Body
  gfx->setTextColor(CLR_PINK); gfx->setCursor(10,y); gfx->print("-- BODY --"); y+=14;
  if (gHeartRateBpm>0) {
    snprintf(buf,24,"%u bpm",gHeartRateBpm);
    row("Heart rate:", buf, CLR_PINK);
    uint8_t mhr=gHRSett.maxHR;
    uint8_t z=gHeartRateBpm<mhr*0.6f?1:gHeartRateBpm<mhr*0.7f?2:
              gHeartRateBpm<mhr*0.8f?3:gHeartRateBpm<mhr*0.9f?4:5;
    snprintf(buf,24,"Zone %u",z);
    row("HR Zone:", buf, z<=2?CLR_GREEN:z<=3?CLR_YELLOW:CLR_RED);
  } else {
    row("Heart rate:", "-- (no device)", CLR_GRAY);
  }

  // Astronomy
  gfx->setTextColor(CLR_YELLOW); gfx->setCursor(10,y); gfx->print("-- ASTRONOMY --"); y+=14;
  if (gGpsFixed && gSunriseH>0) {
    int srH=(int)gSunriseH, srM=(int)((gSunriseH-srH)*60);
    int ssH=(int)gSunsetH,  ssM=(int)((gSunsetH-ssH)*60);
    snprintf(buf,24,"%02d:%02d",srH,srM); row("Sunrise:",buf,CLR_YELLOW);
    snprintf(buf,24,"%02d:%02d",ssH,ssM); row("Sunset:",buf,CLR_ORANGE);
  } else {
    row("Sunrise/Sunset:","No fix",CLR_GRAY);
  }
}

// ── Settings renderer ──────────────────────────────────────────
void drawSettings() {
  uint16_t bg=PAL565[gTheme.bg_screen];
  gfx->fillRect(0,22,TFT_WIDTH,TFT_HEIGHT-40,bg);
  updateBlink(gSett);

  gfx->setTextSize(1); gfx->setTextColor(CLR_CYAN);
  gfx->setCursor(8,28);
  gfx->printf("%s %d/%d", SETT_NAMES[gSett.screen],
    (int)gSett.screen+1, SETT_COUNT);

  // Delegates to per-screen draw function
  switch(gSett.screen) {
    case S_DISPLAY:  drawSettDisplay(); break;
    case S_TIME:     drawSettTime();    break;
    case S_LED:      drawSettLED();     break;
    case S_GPS:      drawSettGPS();     break;
    case S_IMU:      drawSettIMU();     break;
    case S_BATTERY:  drawSettBattery(); break;
    case S_BLE:      drawSettBLE();     break;
    case S_HR:       drawSettHR();      break;
    case S_OTA:      drawSettOTA();     break;
    case S_ODOMETER: drawSettOdo();     break;
  }
}

// Settings screen drawing functions
// (abbreviated — full implementation follows same pattern)
void drawSettRow(uint8_t line,const char*lbl,const char*val,uint8_t curLine,bool editing) {
  uint8_t y=50+line*20;
  bool active=(line==curLine);
  gfx->setTextColor(active?CLR_WHITE:CLR_GRAY);
  gfx->setCursor(10,y); gfx->print(lbl);
  gfx->setTextColor(active?CLR_CYAN:CLR_GRAY);
  gfx->setCursor(180,y); gfx->print(val);
  if (active) {
    bool showUL=!editing || gSett.ulVisible;
    if (showUL) gfx->drawFastHLine(8,y+9,(int)strlen(lbl)*6+175,CLR_CYAN);
  }
}

void drawSettDisplay() {
  char buf[16];
  snprintf(buf,16,"%d%%",gSett.brightness);
  drawSettRow(0,"Brightness:",buf,gSett.activeLine,gSett.isEditing);
  drawSettRow(1,"Auto-scroll:",gSett.autoScroll?"ON":"OFF",gSett.activeLine,gSett.isEditing);
  snprintf(buf,16,"%d sec",gSett.scrollSec);
  drawSettRow(2,"Scroll timer:",buf,gSett.activeLine,gSett.isEditing);
  drawSettRow(3,"Screen BG:",PAL_NAMES[gTheme.bg_screen],gSett.activeLine,gSett.isEditing);
  drawSettRow(4,"Status bar:",PAL_NAMES[gTheme.bg_statusbar],gSett.activeLine,gSett.isEditing);
  drawSettRow(5,"Track line:",PAL_NAMES[gTheme.track_line],gSett.activeLine,gSett.isEditing);
  drawSettRow(6,"Scale bar:",PAL_NAMES[gTheme.scale_bar],gSett.activeLine,gSett.isEditing);
}
void drawSettTime() {
  char buf[12]; snprintf(buf,12,"%.1f h",gTimeSett.utcHalfHours*0.5f);
  drawSettRow(0,"UTC offset:",buf,gSett.activeLine,gSett.isEditing);
  drawSettRow(1,"DST:",gTimeSett.dstEnabled?"ON (EU)":"OFF",gSett.activeLine,gSett.isEditing);
}
void drawSettLED() {
  drawSettRow(0,"Front DRL:",gLedSett.frontEnabled?"ON":"OFF",gSett.activeLine,gSett.isEditing);
  drawSettRow(1,"DRL type:",DRL_NAMES[gLedSett.frontType],gSett.activeLine,gSett.isEditing);
  drawSettRow(2,"Front color:",PAL_NAMES[gLedSett.frontColor],gSett.activeLine,gSett.isEditing);
  char buf[12]; snprintf(buf,12,"%d ms",gLedSett.frontSpeedMs);
  drawSettRow(3,"Turn speed:",buf,gSett.activeLine,gSett.isEditing);
  // NRF rear rows (COMMENTED OUT):
  // drawSettRow(4,"Rear DRL:","DISABLED (NRF off)",gSett.activeLine,gSett.isEditing);
  gfx->setTextColor(CLR_GRAY); gfx->setCursor(10,130); gfx->print("Rear LEDs: NRF disabled");
}
void drawSettGPS() {
  char buf[24];
  snprintf(buf,24,"%.6f",gLat); gfx->setTextColor(CLR_CYAN); gfx->setCursor(10,55); gfx->print("Lat:"); gfx->setCursor(50,55); gfx->print(buf);
  snprintf(buf,24,"%.6f",gLon); gfx->setCursor(10,73); gfx->print("Lon:"); gfx->setCursor(50,73); gfx->print(buf);
  snprintf(buf,24,"%.0f m",gAltM); gfx->setCursor(10,91); gfx->print("Alt:"); gfx->setCursor(50,91); gfx->print(buf);
  gfx->setCursor(10,109); gfx->print("Fix:"); gfx->setTextColor(gGpsFixed?CLR_GREEN:CLR_RED);
  gfx->setCursor(50,109); gfx->print(gGpsFixed?"3D OK":"NO FIX");
  snprintf(buf,24,"%d",gSatCount); gfx->setTextColor(CLR_YELLOW);
  gfx->setCursor(10,127); gfx->print("Sats:"); gfx->setCursor(50,127); gfx->print(buf);
  snprintf(buf,24,"%.1f km/h",gSpeedKmh); gfx->setTextColor(CLR_WHITE);
  gfx->setCursor(10,145); gfx->print("Spd:"); gfx->setCursor(50,145); gfx->print(buf);
}
void drawSettIMU() {
  char buf[24];
  snprintf(buf,24,"%.3fg",gIMUSnapshot.ay); gfx->setTextColor(gIMUSnapshot.isBraking?CLR_RED:CLR_WHITE);
  gfx->setCursor(10,55); gfx->printf("AX:%.3f AY:%s AZ:%.3f",gIMUSnapshot.ax,buf,gIMUSnapshot.az);
  gfx->setTextColor(CLR_WHITE);
  gfx->setCursor(10,73); gfx->printf("Pitch:%.1f Roll:%.1f",gIMUSnapshot.pitch,gIMUSnapshot.roll);
  gfx->setCursor(10,91); gfx->printf("Brake: %d%% Potholes:%u",(int)(gIMUSnapshot.brakeIntensity*100),gIMUSnapshot.potholeCount);
  gfx->setCursor(10,109); gfx->printf("Vib:%.1f/10 Temp:%.1fC",gIMUSnapshot.vibrationRMS,gIMUSnapshot.tempC);
  drawSettRow(0,"[Zero Calibrate]","hold T3 3s",gSett.activeLine,gSett.isEditing);
}
void drawSettBattery() {
  gfx->setCursor(10,55); gfx->setTextColor(CLR_YELLOW); gfx->printf("ESP: %.2fV  %d%%",gBattVolt,(int)gBattPct);
  gfx->setCursor(10,73); gfx->setTextColor(CLR_GRAY);   gfx->print("NRF: -- (disabled)");
  gfx->setCursor(10,91); gfx->setTextColor(CLR_CYAN);   gfx->printf("Heap: %u KB",ESP.getFreeHeap()/1024);
  gfx->setCursor(10,109);
  if (psramFound()) gfx->printf("PSRAM: %u KB",ESP.getFreePsram()/1024);
  else              gfx->print("PSRAM: N/A");
  gfx->setCursor(10,127);gfx->setTextColor(CLR_ORANGE);
  // temperatureRead() is internal/undocumented; guard for future Core changes.
  #if defined(SOC_TEMP_SENSOR_SUPPORTED) || defined(CONFIG_IDF_TARGET_ESP32S3)
    gfx->printf("Die temp: %.1f C",temperatureRead());
  #else
    gfx->print("Die temp: N/A");
  #endif
  gfx->setCursor(10,145);gfx->setTextColor(CLR_WHITE);  gfx->printf("Uptime: %lu s",millis()/1000);
  if (gErrors.hasActive()) {
    gfx->setCursor(10,165); gfx->setTextColor(CLR_RED); gfx->print("ERRORS:");
    gfx->setCursor(10,180); gfx->print(ERR_NAMES[gErrors.firstActive()]);
  }
}
void drawSettBLE() {
  gfx->setTextSize(1);
  gfx->setCursor(10,55); gfx->setTextColor(CLR_CYAN); gfx->print("WiFi MAC:"); gfx->print(WiFi.macAddress());
  gfx->setCursor(10,73); gfx->setTextColor(gBLE.hrConnected()?CLR_GREEN:CLR_YELLOW);
  gfx->printf("Garmin HR: %s", gBLE.hrConnected()?"Connected":"Scanning...");
  gfx->setCursor(10,91); gfx->setTextColor(CLR_GRAY); gfx->print("NRF: DISABLED in firmware");
  gfx->setCursor(10,109); gfx->setTextColor(CLR_WHITE);
  gfx->printf("HR val: %u bpm", gHeartRateBpm);
}
void drawSettHR() {
  gfx->setTextColor(CLR_PINK); gfx->setCursor(10,55);
  gfx->printf("Current: %u bpm", gHeartRateBpm);
  char buf[12]; snprintf(buf,12,"%d bpm",gHRSett.maxHR);
  drawSettRow(0,"Max HR:", buf, gSett.activeLine, gSett.isEditing);
  uint8_t mhr=gHRSett.maxHR;
  gfx->setTextColor(CLR_GRAY); gfx->setCursor(10,90);
  gfx->printf("Z1<%d Z2<%d Z3<%d Z4<%d Z5>=%d",
    (int)(mhr*0.6f),(int)(mhr*0.7f),(int)(mhr*0.8f),(int)(mhr*0.9f),(int)(mhr*0.9f));
}
void drawSettOTA() {
  gfx->setTextSize(1);
  gfx->setCursor(10,55); gfx->setTextColor(PAL565[COL_VIOLET]);
  gfx->printf("FW: %s  Build: %s", FW_VERSION, __DATE__);
  if (gOTA.active) {
    gfx->setCursor(10,75); gfx->setTextColor(CLR_GREEN);
    gfx->printf("AP: %s  pass: %s",OTA_SSID,OTA_PASS);
    gfx->setCursor(10,93); gfx->print("IP: 192.168.4.1");
    gfx->setCursor(10,111); gfx->printf("Uptime: %lus", gOTA.uptimeSec());
  }
  drawSettRow(0,gOTA.active?"[Stop OTA]":"[Start OTA]",
    gOTA.active?"Active":"Stopped",gSett.activeLine,gSett.isEditing);
}
void drawSettOdo() {
  gfx->setTextSize(2); gfx->setTextColor(CLR_WHITE);
  gfx->setCursor(40,55); gfx->printf("%.1f km",gTotalDistKm);
  gfx->setTextSize(1); gfx->setTextColor(CLR_CYAN);
  gfx->setCursor(10,90); gfx->printf("Session: %.2f km",gSessionDistKm);
  drawSettRow(0,"[Reset session]","hold T3 3s",gSett.activeLine,gSett.isEditing);
  drawSettRow(1,"[Reset total]","hold T3 5s",gSett.activeLine,gSett.isEditing);
}

// =============================================================
//  SESSION RESET HELPERS (after all globals declared)
// =============================================================
void resetSession() {
  gSessionDistKm = 0;
  gSessionStartMs = millis();
  gMaxSpeed = 0;
  gAvgSpeedAcc = 0;
  gAvgSpeedN = 0;
  // Track + bounding box
  gTrackLen = 0;
  gMinLat=1e6; gMaxLat=-1e6;
  gMinLon=1e6; gMaxLon=-1e6;
  gHasPrevPos = false;
  gMapOffX=0; gMapOffY=0; gMapScale=1.0f; gMapMode=0;
  DBG("Session reset");
}

// =============================================================
//  SETUP
// =============================================================
void setup() {
  Serial.begin(SERIAL_BAUD);
  DBG("=== ESP32 Bike Computer v%s ===", FW_VERSION);

  // --- PSRAM check ---
  checkModulePSRAM();
  if (psramFound()) {
    gTrack=(TrackPoint*)heap_caps_malloc(TRACK_MAX_PTS*sizeof(TrackPoint),MALLOC_CAP_SPIRAM);
    DBG("Track buffer: %u bytes in PSRAM", TRACK_MAX_PTS*sizeof(TrackPoint));
  } else {
    gTrack=new TrackPoint[TRACK_MAX_PTS];
    DBG("Track buffer: %u bytes in heap", TRACK_MAX_PTS*sizeof(TrackPoint));
  }

  // --- Display ---
  DBG("INIT: Display...");
  // --- Display backlight (ESP32 Arduino Core 3.x API) ---
  // Core 3.x: ledcSetup+ledcAttachPin replaced by single ledcAttach(pin, freq, resolution)
  // All subsequent ledcWrite calls use PIN directly, not channel number.
  ledcAttach(PIN_TFT_BL, BL_LEDC_FREQ, BL_LEDC_RES);
  ledcWrite(PIN_TFT_BL, 200);
  if (!gfx->begin(TFT_SPI_FREQ)) {
    Serial.println("[CRITICAL] Display init FAILED!");
    gErrors.set(ERR_DISPLAY, SEV_CRITICAL);
  } else {
    DBG("INIT: Display OK");
  }
  gfx->fillScreen(CLR_BLACK);
  gfx->setTextColor(CLR_WHITE); gfx->setTextSize(2);
  gfx->setCursor(60,160); gfx->print("Bike Computer");
  gfx->setCursor(100,185); gfx->setTextSize(1);
  gfx->printf("v%s", FW_VERSION);

  // --- TE (Tearing Effect) input — FPS counter ISR (v1.1.2) ---
  // Display pulses TE on each frame complete. We count pulses for FPS metric.
  // Full sync (wait for TE before redraw) deferred to v1.2 with partial redraws.
  pinMode(PIN_TFT_TE, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_TFT_TE), teISR, RISING);
  DBG("INIT: TE ISR attached on GPIO%d", PIN_TFT_TE);

  // --- LittleFS mount (v1.1.2) ---
  // Partition table reserves ~9.875MB for FS. Format on first boot if mount fails.
  // Currently no files are written by firmware; reserved for GPX track export
  // and crash logs in upcoming versions.
  DBG("INIT: LittleFS...");
  if (!LittleFS.begin(true)) {  // true = format if mount fails
    DBG("[WARN] LittleFS mount failed — FS unavailable");
    gErrors.set(ERR_FS, SEV_WARN);
  } else {
    DBG("INIT: LittleFS OK — %u/%u KB used",
        (unsigned)(LittleFS.usedBytes()/1024),
        (unsigned)(LittleFS.totalBytes()/1024));
  }

  // --- Load preferences ---
  DBG("INIT: Loading settings...");
  loadTheme(gTheme, gPrefs);
  loadSettings(gSett, gLedSett, gTimeSett, gHRSett, gPrefs);
  gPrefs.begin(PREF_BIKE, true);
  gTotalDistKm = gPrefs.getFloat(PREF_TOTAL, 0);
  gPrefs.end();
  DBG("Odometer: %.2f km", gTotalDistKm);

  // --- GPS ---
  DBG("INIT: GPS UART1...");
  // HardwareSerial::begin signature: (baud, config, rxPin, txPin)
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX_PIN, PIN_GPS_TX_PIN);
  delay(100);
  DBG("INIT: GPS UART started (waiting for data)");

  // --- IMU ---
  DBG("INIT: IMU MPU6500...");
  bool imuOK = gIMU.begin(0);
  checkModuleIMU(imuOK);
  if (imuOK) gIMU.loadCalibration(gPrefs);

  // --- LEDs ---
  DBG("INIT: WS2812B front...");
  gLED.begin();
  gLED.setBrightness(gSett.brightness);

  // --- Encoder ---
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  pinMode(PIN_ENC_SW,  INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_DT),  encoderISR, CHANGE);
  DBG("INIT: Encoder OK");

  // --- Buttons (TTP223 modules, push-pull HIGH=pressed) ---
  // INPUT_PULLDOWN added as safety in case module is disconnected
  // (TTP223 push-pull output overpowers internal 45kΩ pulldown).
  DBG("INIT: Buttons BTN1/BTN2/BTN3 (TTP223, active HIGH)");
  pinMode(PIN_BTN1, INPUT_PULLDOWN);
  pinMode(PIN_BTN2, INPUT_PULLDOWN);
  pinMode(PIN_BTN3, INPUT_PULLDOWN);

  // --- Battery ADC ---
  // ADC_ATTEN_DB_11/12 renamed in Core 3.x. Use numeric value 3 = 11dB (0–3.3V range).
  analogSetAttenuation((adc_attenuation_t)3);
  DBG("INIT: Battery ADC GPIO%d", PIN_VBAT);

  // --- BLE ---
  DBG("INIT: BLE Central (Garmin HR)...");
  gBLE.begin();

  // --- IMU Task on Core 0 ---
  if (imuOK) {
    xTaskCreatePinnedToCore(imuTask,"IMU",4096,nullptr,1,&hIMUTask,0);
    DBG("INIT: IMU task started on Core 0");
  }

  gSessionStartMs = millis();
  gLastAstroMs    = 0; // force first calc
  DBG("INIT: Done. Starting main loop.");
  delay(800);
  gDisplayDirty = true;
}

// =============================================================
//  LOOP
// =============================================================
void loop() {
  uint32_t now = millis();

  // IMU thread-safe snapshot (read once per loop, used everywhere else)
  refreshIMUSnapshot();

  // Process calibration request from web/UI in main thread
  // (avoids Preferences race between IMU task and main loop)
  if (gCalibRequested) {
    gCalibRequested = false;
    // Stop IMU task briefly to avoid concurrent NVS access
    if (hIMUTask) vTaskSuspend(hIMUTask);
    gIMU.calibrate(gPrefs);
    if (hIMUTask) vTaskResume(hIMUTask);
    DBG("IMU calibration completed");
  }

  // GPS
  processGPS(now);

  // Battery
  readBattery(now);

  // Astronomy
  updateAstronomy(now);

  // OTA tick
  gOTA.tick();

  // BLE tick (reconnect logic)
  gBLE.tick();

  // Encoder + Buttons
  handleEncoder();
  handleButtons();

  // IMU calibration from BTN3 hold in settings — request via flag
  static uint32_t b3HoldMs=0;
  if (gInSettings && gSett.screen==S_IMU && btnPressed(PIN_BTN3)) {
    if (b3HoldMs==0) b3HoldMs=now;
    else if (now-b3HoldMs>3000) {
      gCalibRequested = true; b3HoldMs=0;
      DBG("IMU calibration requested via BTN3");
    }
  } else b3HoldMs=0;

  // LED front animation
  gLED.tick(now, gLedSett.frontColor, gLedSett.frontEnabled, gSett.brightness);

  // Auto-scroll main screens
  if (!gInSettings && gSett.autoScroll) {
    uint32_t tms=(uint32_t)gSett.scrollSec*1000;
    if (now-gLastScrollMs >= tms) {
      gScreen=(gScreen+1)%SCR_COUNT;
      gLastScrollMs=now;
      gDisplayDirty=true;
    }
  }

  // Display update — separate throttling for status bar and main area
  if (gDisplayDirty || now-gLastSbarMs>=SBAR_UPD_MS) {
    gLastSbarMs=now;
    drawStatusBar();
  }
  if (gDisplayDirty || now-gLastDrawMs>=DISP_UPD_MS) {
    gDisplayDirty=false;
    gLastDrawMs=now;
    waitForTE();
    if (gInSettings) {
      drawSettings();
    } else {
      switch(gScreen) {
        case SCR_RIDING:  drawScreenRiding();  break;
        case SCR_MAP:     drawScreenMap();     break;
        case SCR_TERRAIN: drawScreenTerrain(); break;
      }
    }
    drawErrorOverlay();
  }
}
