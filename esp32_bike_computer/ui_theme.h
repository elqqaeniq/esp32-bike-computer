#pragma once
#include <Arduino.h>
#include <Preferences.h>

// 11 palette colors
enum ColorIndex : uint8_t {
  COL_WHITE=0,COL_BLACK,COL_GREEN,COL_TEAL,COL_CYAN,
  COL_INDIGO,COL_VIOLET,COL_PINK,COL_RED,COL_ORANGE,COL_YELLOW,
  COL_COUNT=11
};

static const uint16_t PAL565[COL_COUNT] = {
  0xFFFF,0x0000,0x07E0,0x07FF,0x4DFE,
  0x3819,0x781F,0xF81F,0xF800,0xFD00,0xFFE0
};
static const char* PAL_NAMES[COL_COUNT] = {
  "White","Black","Green","Teal","Cyan",
  "Indigo","Violet","Pink","Red","Orange","Yellow"
};

struct RGB888 { uint8_t r,g,b; };
static const RGB888 PAL888[COL_COUNT] = {
  {255,255,255},{0,0,0},{0,255,0},{0,255,200},{77,176,255},
  {48,48,200},{120,0,248},{255,0,255},{255,0,0},{255,104,0},{255,224,0}
};

struct UITheme {
  uint8_t bg_screen;
  uint8_t bg_statusbar;
  uint8_t track_line;
  uint8_t scale_bar;
};

inline UITheme defaultTheme() {
  return {COL_BLACK, COL_INDIGO, COL_CYAN, COL_YELLOW};
}
extern UITheme gTheme;

inline uint16_t statusbarTextColor() {
  uint16_t c = PAL565[gTheme.bg_statusbar];
  uint8_t r=(c>>11)&0x1F, g=(c>>5)&0x3F, b=c&0x1F;
  float luma = r*0.299f/31+g*0.587f/63+b*0.114f/31;
  return luma<0.4f ? 0xFFFF : 0x0000;
}

// Battery color interpolation
inline RGB888 lerpRGB(RGB888 a, RGB888 b, float t) {
  return {(uint8_t)(a.r+(b.r-a.r)*t),(uint8_t)(a.g+(b.g-a.g)*t),(uint8_t)(a.b+(b.b-a.b)*t)};
}

inline uint16_t battColor565(uint8_t pct) {
  static const RGB888 N[4]={{74,222,128},{96,165,250},{250,204,21},{248,113,113}};
  RGB888 c;
  if      (pct>=75) c=lerpRGB(N[1],N[0],(pct-75)/25.0f);
  else if (pct>=50) c=lerpRGB(N[2],N[1],(pct-50)/25.0f);
  else if (pct>=25) c=lerpRGB(N[3],N[2],(pct-25)/25.0f);
  else c=N[3];
  return ((c.r>>3)<<11)|((c.g>>2)<<5)|(c.b>>3);
}

// Battery icon dimensions
#define BATT_W  28
#define BATT_H  14
#define BATT_NW  4
#define BATT_NH 11
#define BATT_R   2

// Colors (fixed)
#define CLR_WHITE   0xFFFF
#define CLR_BLACK   0x0000
#define CLR_ORANGE  0xFD00
#define CLR_RED     0xF800
#define CLR_GREEN   0x07E0
#define CLR_YELLOW  0xFFE0
#define CLR_GRAY    0x4208
#define CLR_CYAN    0x07FF
#define CLR_PINK    0xF81F
#define CLR_BLUE    0x4DFE
#define CLR_GPS_OK  0x07E0
#define CLR_GPS_ERR 0xF800

// Theme persistence
#define TH_NS    "theme"
#define TH_BG    "bg"
#define TH_SB    "sb"
#define TH_TR    "tr"
#define TH_SC    "sc"

inline void loadTheme(UITheme &t, Preferences &p) {
  UITheme d=defaultTheme();
  p.begin(TH_NS,true);
  t.bg_screen   =p.getUChar(TH_BG,d.bg_screen);
  t.bg_statusbar=p.getUChar(TH_SB,d.bg_statusbar);
  t.track_line  =p.getUChar(TH_TR,d.track_line);
  t.scale_bar   =p.getUChar(TH_SC,d.scale_bar);
  p.end();
}
inline void saveTheme(const UITheme &t, Preferences &p) {
  p.begin(TH_NS,false);
  p.putUChar(TH_BG,t.bg_screen);
  p.putUChar(TH_SB,t.bg_statusbar);
  p.putUChar(TH_TR,t.track_line);
  p.putUChar(TH_SC,t.scale_bar);
  p.end();
}
