#pragma once
// =============================================================
//  SETTINGS.H v3 — 10 settings screens
// =============================================================
#include <Preferences.h>
#include "config.h"
#include "ui_theme.h"

#define SETT_COUNT 10
enum SettScreen:uint8_t {
  S_DISPLAY=0,S_TIME,S_LED,S_GPS,S_IMU,
  S_BATTERY,S_BLE,S_HR,S_OTA,S_ODOMETER
};
static const char* SETT_NAMES[SETT_COUNT] = {
  "DISPLAY","TIME","WS2812 LED","GPS INFO","IMU (MPU6500)",
  "BATTERY & SYSTEM","BLE & WIFI","HEART RATE","OTA UPDATE","ODOMETER"
};
static const uint8_t SETT_LINES[SETT_COUNT] = {
  7,  // DISPLAY: bright, autoscroll, timer, bg, sbar, track, scale
  2,  // TIME: utc offset, dst
  8,  // LED: 4 front + 4 rear (rear commented)
  0,  // GPS: readonly
  3,  // IMU: calibrate, brake_thr, brake_max
  1,  // BATTERY: clear log
  0,  // BLE: readonly
  1,  // HR: max hr setting
  1,  // OTA: start/stop
  2,  // ODOMETER: reset session, reset total
};

enum LedDrlType:uint8_t { DRL_STATIC=0, DRL_BREATHING, DRL_CHASE };
static const char* DRL_NAMES[]={"Static","Breathing","Chase"};

struct LedSettings {
  bool       frontEnabled;
  LedDrlType frontType;
  uint8_t    frontColor;
  uint16_t   frontSpeedMs;
  // NRF rear (commented out):
  // bool rearEnabled; LedDrlType rearType; uint8_t rearColor; uint16_t rearSpeedMs;
};

struct TimeSettings {
  int8_t utcHalfHours; // UTC offset in 0.5h steps
  bool   dstEnabled;
};

struct HRSettings {
  uint8_t maxHR;   // for zone calculation
};

struct SettingsState {
  SettScreen screen      = S_DISPLAY;
  uint8_t    activeLine  = 0;
  bool       isEditing   = false;
  uint32_t   editStartMs = 0;
  bool       ulVisible   = true;
  // Display
  uint8_t  brightness    = 80;
  bool     autoScroll    = true;
  uint8_t  scrollSec     = 5;
};

inline void updateBlink(SettingsState &s) {
  if (!s.isEditing) { s.ulVisible=true; return; }
  s.ulVisible = ((millis()-s.editStartMs)%1000) < 500;
}

inline void toggleEdit(SettingsState &s) {
  s.isEditing   = !s.isEditing;
  s.editStartMs = millis();
  s.ulVisible   = true;
}

inline void settPrev(SettingsState &s) {
  if (s.isEditing) return;
  s.screen=(SettScreen)((s.screen+SETT_COUNT-1)%SETT_COUNT);
  s.activeLine=0; s.isEditing=false;
}
inline void settNext(SettingsState &s) {
  if (s.isEditing) return;
  s.screen=(SettScreen)((s.screen+1)%SETT_COUNT);
  s.activeLine=0; s.isEditing=false;
}

// Handle encoder rotation in settings
inline bool settRotate(SettingsState &s, int8_t d,
    UITheme &theme, LedSettings &led,
    TimeSettings &time, HRSettings &hr, Preferences &prefs) {
  uint8_t maxL = SETT_LINES[s.screen];
  if (!s.isEditing) {
    if (maxL==0) return false;
    s.activeLine = (s.activeLine+maxL+d)%maxL;
    return true;
  }
  switch(s.screen) {
    case S_DISPLAY:
      if (s.activeLine==0) s.brightness=constrain((int)s.brightness+d*5,10,100);
      else if (s.activeLine==1) s.autoScroll=!s.autoScroll;
      else if (s.activeLine==2) {
        uint8_t t[]={3,5,9};
        uint8_t i=(s.scrollSec==3)?0:(s.scrollSec==5)?1:2;
        i=(i+3+d)%3; s.scrollSec=t[i];
      }
      else {
        uint8_t *cf=nullptr;
        if(s.activeLine==3) cf=&theme.bg_screen;
        else if(s.activeLine==4) cf=&theme.bg_statusbar;
        else if(s.activeLine==5) cf=&theme.track_line;
        else if(s.activeLine==6) cf=&theme.scale_bar;
        if(cf) { *cf=(*cf+COL_COUNT+d)%COL_COUNT; saveTheme(theme,prefs); }
      }
      break;
    case S_TIME:
      if(s.activeLine==0) time.utcHalfHours=constrain((int)time.utcHalfHours+d,-24,28);
      else time.dstEnabled=!time.dstEnabled;
      break;
    case S_LED:
      switch(s.activeLine){
        case 0: led.frontEnabled=!led.frontEnabled; break;
        case 1: led.frontType=(LedDrlType)((led.frontType+3+d)%3); break;
        case 2: led.frontColor=(led.frontColor+COL_COUNT+d)%COL_COUNT; break;
        case 3: led.frontSpeedMs=constrain((int)led.frontSpeedMs+d*10,60,500); break;
        // cases 4-7: NRF rear (commented out)
      }
      break;
    case S_IMU:
      break; // handled via T3 hold
    case S_HR:
      if(s.activeLine==0) hr.maxHR=constrain((int)hr.maxHR+d,120,220);
      break;
    default: break;
  }
  return true;
}

// Persistence — uses PREF_SETT namespace from config.h ("settings")
#define SN PREF_SETT
inline void saveSettings(const SettingsState &s, const LedSettings &l,
    const TimeSettings &t, const HRSettings &h, Preferences &p) {
  p.begin(SN,false);
  p.putUChar("bright",s.brightness);
  p.putBool("ascrl",s.autoScroll);
  p.putUChar("scrt",s.scrollSec);
  p.putChar("utc",t.utcHalfHours);
  p.putBool("dst",t.dstEnabled);
  p.putBool("fen",l.frontEnabled);
  p.putUChar("ftp",(uint8_t)l.frontType);
  p.putUChar("fcl",l.frontColor);
  p.putUShort("fsp",l.frontSpeedMs);
  p.putUChar("mhr",h.maxHR);
  p.end();
}
inline void loadSettings(SettingsState &s, LedSettings &l,
    TimeSettings &t, HRSettings &h, Preferences &p) {
  p.begin(SN,true);
  s.brightness  = p.getUChar("bright",80);
  s.autoScroll  = p.getBool("ascrl",true);
  s.scrollSec   = p.getUChar("scrt",5);
  t.utcHalfHours= p.getChar("utc",4);
  t.dstEnabled  = p.getBool("dst",true);
  l.frontEnabled= p.getBool("fen",true);
  l.frontType   = (LedDrlType)p.getUChar("ftp",0);
  l.frontColor  = p.getUChar("fcl",COL_ORANGE);
  l.frontSpeedMs= p.getUShort("fsp",120);
  h.maxHR       = p.getUChar("mhr",190);
  p.end();
}
