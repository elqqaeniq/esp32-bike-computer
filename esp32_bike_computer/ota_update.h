#pragma once
// =============================================================
//  OTA_UPDATE.H — WiFi Captive Portal OTA + Settings Web UI
//  Library: ESPAsyncWebServer + AsyncTCP + DNSServer (built-in)
// =============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>
#include "config.h"
#include "settings.h"
#include "ui_theme.h"
#include "error_handler.h"

// External state refs (defined in main .ino)
extern SettingsState gSett;
extern LedSettings   gLedSett;
extern TimeSettings  gTimeSett;
extern HRSettings    gHRSett;
extern UITheme       gTheme;
extern Preferences   gPrefs;
extern float         gTotalDistKm;
extern float         gSessionDistKm;
extern volatile uint16_t gHeartRateBpm;
extern float         gSpeedKmh;
extern float         gBattPct;
extern IMUData       gIMUSnapshot;  // thread-safe IMU snapshot

// ── HTML pages stored in PROGMEM ─────────────────────────────

static const char HTML_HEAD[] PROGMEM = R"(<!DOCTYPE html>
<html><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:monospace;background:#0d1117;color:#e6edf3;padding:16px}
h1{color:#f59e0b;font-size:15px;letter-spacing:2px;margin-bottom:4px}
.sub{color:#7d8590;font-size:11px;margin-bottom:20px}
nav{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:20px}
nav a{padding:6px 12px;background:#161b22;border:1px solid #30363d;
  border-radius:6px;color:#e6edf3;text-decoration:none;font-size:11px}
nav a.active{border-color:#f59e0b;color:#f59e0b}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;
  margin-bottom:16px;overflow:hidden}
.card-h{padding:8px 14px;border-bottom:1px solid #30363d;
  font-size:11px;letter-spacing:1px;color:#7d8590}
.card-b{padding:12px 14px}
.row{display:flex;justify-content:space-between;align-items:center;
  padding:5px 0;border-bottom:1px solid #1a1f28;font-size:12px;gap:8px}
.row:last-child{border-bottom:none}
.lbl{color:#7d8590}
.val{color:#e6edf3;text-align:right}
.g{color:#4ade80}.y{color:#fbbf24}.r{color:#f87171}
.o{color:#fb923c}.c{color:#22d3ee}.p{color:#a78bfa}.pk{color:#f472b6}
input[type=range]{width:120px;accent-color:#f59e0b}
input[type=number]{background:#0d1117;border:1px solid #30363d;
  border-radius:4px;color:#e6edf3;padding:3px 6px;width:70px;font-family:monospace}
select{background:#0d1117;border:1px solid #30363d;border-radius:4px;
  color:#e6edf3;padding:3px 6px;font-family:monospace}
.btn{padding:8px 16px;border:none;border-radius:6px;font-family:monospace;
  font-size:12px;cursor:pointer;letter-spacing:1px}
.btn-o{background:#f59e0b;color:#000}
.btn-r{background:#f87171;color:#000}
.btn-g{background:#4ade80;color:#000}
.btn-p{background:#a78bfa;color:#000}
.prog{background:#1a1f28;border-radius:4px;height:8px;margin:8px 0;overflow:hidden}
.prog-bar{background:linear-gradient(90deg,#2e6bd4,#a78bfa);height:100%;
  width:0%;transition:width 0.3s;border-radius:4px}
.err{background:#1a0505;border:1px solid #5a0000;border-radius:6px;
  padding:8px 12px;font-size:11px;color:#f87171;margin-bottom:12px}
.info{background:#001520;border:1px solid #003040;border-radius:6px;
  padding:8px 12px;font-size:11px;color:#22d3ee;margin-bottom:12px}
form{display:inline}
</style></head><body>
<h1>🚴 BIKE COMPUTER</h1>
<p class="sub">ESP32-S3-N16R8 · )";

static const char HTML_NAV[] PROGMEM = R"(</p>
<nav>
<a href="/">Home</a>
<a href="/settings/display">Display</a>
<a href="/settings/time">Time</a>
<a href="/settings/led">LED</a>
<a href="/settings/gps">GPS</a>
<a href="/settings/imu">IMU</a>
<a href="/settings/battery">Battery</a>
<a href="/settings/ble">BLE</a>
<a href="/settings/hr">Heart Rate</a>
<a href="/settings/ota">OTA Update</a>
<a href="/settings/odometer">Odometer</a>
</nav>)";

// ── OtaManager ───────────────────────────────────────────────
class OtaManager {
public:
  bool   active     = false;
  uint8_t progress  = 0;
  uint32_t speedKbs = 0;
  String statusMsg  = "Idle";

  void begin() { /* call start() from settings */ }

  bool start() {
    // v1.1.2: Force-save odometer before tearing down BLE / starting AP.
    // Reason: OTA process consumes ~1MB RAM and can fail; if we crash mid-way,
    // ODOM_SAVE_KM granularity (1km) means we could lose up to ~1km of mileage.
    // This save is cheap (one float) and guarantees pre-OTA odometer state.
    extern float gTotalDistKm;
    extern float gLastSavedKm;
    extern Preferences gPrefs;
    gPrefs.begin(PREF_BIKE, false);
    gPrefs.putFloat(PREF_TOTAL, gTotalDistKm);
    gPrefs.end();
    gLastSavedKm = gTotalDistKm;
    DBG("OTA: pre-start odometer save: %.2f km", gTotalDistKm);

    // Free BLE before bringing up WiFi+AsyncWebServer.
    // Coexistence on ESP32-S3 works but RAM is tight during firmware upload.
    extern BLEManager gBLE;
    gBLE.end();
    delay(50);

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(
      IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
    WiFi.softAP(OTA_SSID, OTA_PASS);
    _dns.start(53, "*", IPAddress(192,168,4,1));
    _setupRoutes();
    _server.begin();
    active    = true;
    progress  = 0;
    statusMsg = "AP active";
    _startMs  = millis();
    _lastActMs= millis();
    DBG("OTA: AP started %s @ 192.168.4.1", OTA_SSID);
    return true;
  }

  void stop() {
    if (!active) return;
    _server.end(); _dns.stop();
    WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF);
    active    = false;
    statusMsg = "Stopped";
    // Restart BLE after WiFi is fully off
    delay(100);
    extern BLEManager gBLE;
    gBLE.begin();
    DBG("OTA: stopped, BLE restarted");
  }

  void tick() {
    if (!active) return;
    _dns.processNextRequest();
    if (millis()-_lastActMs > OTA_TIMEOUT) {
      DBG("OTA: timeout");
      stop();
    }
  }

  uint32_t uptimeSec() { return active ? (millis()-_startMs)/1000 : 0; }

private:
  AsyncWebServer _server{80};
  DNSServer      _dns;
  uint32_t       _startMs   = 0;
  uint32_t       _lastActMs = 0;
  uint32_t       _upMs      = 0;
  size_t         _written    = 0;

  void _touch() { _lastActMs = millis(); }

  // ── Route helpers ──────────────────────────────────────────
  String _head(const String &title, const String &activeHref) {
    String h = FPSTR(HTML_HEAD);
    h += FW_VERSION;
    h += FPSTR(HTML_NAV);
    // mark active nav link
    h.replace("href=\"" + activeHref + "\"",
              "href=\"" + activeHref + "\" class=\"active\"");
    h += "<h2 style='color:#22d3ee;font-size:13px;margin-bottom:16px;letter-spacing:2px'>";
    h += title + "</h2>";
    return h;
  }
  String _foot() { return "</body></html>"; }

  String _row(const String &lbl, const String &val, const String &cls="val") {
    return "<div class='row'><span class='lbl'>" + lbl + "</span>"
           "<span class='" + cls + "'>" + val + "</span></div>";
  }

  String _palSelect(const char* name, uint8_t cur) {
    String s = "<select name='" + String(name) + "'>";
    for (uint8_t i=0; i<COL_COUNT; i++) {
      s += "<option value='" + String(i) + "'";
      if (i==cur) s += " selected";
      s += ">" + String(PAL_NAMES[i]) + "</option>";
    }
    s += "</select>";
    return s;
  }

  // ── /errors JSON ──────────────────────────────────────────
  void _handleErrors(AsyncWebServerRequest *r) {
    _touch();
    r->send(200,"application/json", gErrors.toJSON());
  }

  // ── / Home ────────────────────────────────────────────────
  void _handleRoot(AsyncWebServerRequest *r) {
    _touch();
    String h = _head("Home", "/");
    // Error banner
    if (gErrors.hasActive()) {
      h += "<div class='err'>⚠ System errors: ";
      for (uint8_t i=0; i<ErrorHandler::MAX_ERRORS; i++) {
        auto &e = gErrors.entries()[i];
        if (e.active) h += "<br>• " + String(ERR_NAMES[e.code]);
      }
      h += "</div>";
    } else {
      h += "<div class='info'>✓ All systems operational</div>";
    }
    h += "<div class='card'><div class='card-h'>LIVE DATA</div><div class='card-b'>";
    h += _row("Speed",     String(gSpeedKmh,1)+" km/h");
    h += _row("Session",   String(gSessionDistKm,2)+" km");
    h += _row("Total",     String(gTotalDistKm,1)+" km");
    h += _row("Heart Rate",gHeartRateBpm>0 ? String(gHeartRateBpm)+" bpm" : "--", "pk");
    h += _row("Battery",   String((int)gBattPct)+"%",  gBattPct>50?"g":gBattPct>20?"y":"r");
    h += _row("Free Heap", String(ESP.getFreeHeap()/1024)+" KB");
    h += _row("PSRAM",     psramFound()?String(ESP.getFreePsram()/1024)+" KB":"N/A");
    h += _row("Uptime",    String(millis()/1000)+" s");
    h += "</div></div>" + _foot();
    r->send(200,"text/html", h);
  }

  // ── /settings/display ─────────────────────────────────────
  void _handleDisplay(AsyncWebServerRequest *r) {
    _touch();
    String h = _head("Display Settings", "/settings/display");
    h += "<form action='/save/display' method='POST'>";
    h += "<div class='card'><div class='card-h'>BRIGHTNESS & SCROLL</div><div class='card-b'>";
    h += "<div class='row'><span class='lbl'>Brightness</span>"
         "<input type='range' name='bright' min='10' max='100' step='5' value='"
         + String(gSett.brightness) + "' oninput=\"this.nextSibling.textContent=this.value+'%'\">"
         "<span class='val'>" + String(gSett.brightness) + "%</span></div>";
    h += "<div class='row'><span class='lbl'>Auto-scroll</span>"
         "<select name='ascrl'><option value='1'" + String(gSett.autoScroll?" selected":"") + ">ON</option>"
         "<option value='0'" + String(!gSett.autoScroll?" selected":"") + ">OFF</option></select></div>";
    h += "<div class='row'><span class='lbl'>Scroll timer</span>"
         "<select name='scrt'>"
         "<option value='3'" + String(gSett.scrollSec==3?" selected":"") + ">3 sec</option>"
         "<option value='5'" + String(gSett.scrollSec==5?" selected":"") + ">5 sec</option>"
         "<option value='9'" + String(gSett.scrollSec==9?" selected":"") + ">9 sec</option>"
         "</select></div>";
    h += "</div></div>";
    h += "<div class='card'><div class='card-h'>COLORS (11 palette)</div><div class='card-b'>";
    h += "<div class='row'><span class='lbl'>Screen BG</span>" + _palSelect("bg",gTheme.bg_screen) + "</div>";
    h += "<div class='row'><span class='lbl'>Status bar</span>" + _palSelect("sb",gTheme.bg_statusbar) + "</div>";
    h += "<div class='row'><span class='lbl'>Track line</span>" + _palSelect("tr",gTheme.track_line) + "</div>";
    h += "<div class='row'><span class='lbl'>Scale bar</span>"  + _palSelect("sc",gTheme.scale_bar) + "</div>";
    h += "</div></div>";
    h += "<button class='btn btn-o' type='submit'>SAVE</button></form>";
    h += _foot();
    r->send(200,"text/html",h);
  }

  void _saveDisplay(AsyncWebServerRequest *r) {
    _touch();
    if (r->hasParam("bright",true)) gSett.brightness = r->getParam("bright",true)->value().toInt();
    if (r->hasParam("ascrl",true))  gSett.autoScroll  = r->getParam("ascrl",true)->value()=="1";
    if (r->hasParam("scrt",true))   gSett.scrollSec   = r->getParam("scrt",true)->value().toInt();
    if (r->hasParam("bg",true))     gTheme.bg_screen    = r->getParam("bg",true)->value().toInt();
    if (r->hasParam("sb",true))     gTheme.bg_statusbar = r->getParam("sb",true)->value().toInt();
    if (r->hasParam("tr",true))     gTheme.track_line   = r->getParam("tr",true)->value().toInt();
    if (r->hasParam("sc",true))     gTheme.scale_bar    = r->getParam("sc",true)->value().toInt();
    saveTheme(gTheme,gPrefs);
    saveSettings(gSett,gLedSett,gTimeSett,gHRSett,gPrefs);
    r->redirect("/settings/display");
  }

  // ── /settings/time ────────────────────────────────────────
  void _handleTime(AsyncWebServerRequest *r) {
    _touch();
    String h = _head("Time Settings", "/settings/time");
    h += "<form action='/save/time' method='POST'>";
    h += "<div class='card'><div class='card-h'>TIMEZONE</div><div class='card-b'>";
    float off = gTimeSett.utcHalfHours * 0.5f;
    h += "<div class='row'><span class='lbl'>UTC offset (hours)</span>"
         "<input type='number' name='utc' min='-12' max='14' step='0.5' value='"
         + String(off,1) + "'></div>";
    h += "<div class='row'><span class='lbl'>DST (EU auto)</span>"
         "<select name='dst'><option value='1'" + String(gTimeSett.dstEnabled?" selected":"") + ">ON</option>"
         "<option value='0'" + String(!gTimeSett.dstEnabled?" selected":"") + ">OFF</option></select></div>";
    h += "</div></div><button class='btn btn-o' type='submit'>SAVE</button></form>" + _foot();
    r->send(200,"text/html",h);
  }

  void _saveTime(AsyncWebServerRequest *r) {
    _touch();
    if (r->hasParam("utc",true))
      gTimeSett.utcHalfHours = (int8_t)(r->getParam("utc",true)->value().toFloat() * 2);
    if (r->hasParam("dst",true))
      gTimeSett.dstEnabled = r->getParam("dst",true)->value()=="1";
    saveSettings(gSett,gLedSett,gTimeSett,gHRSett,gPrefs);
    r->redirect("/settings/time");
  }

  // ── /settings/led ─────────────────────────────────────────
  void _handleLED(AsyncWebServerRequest *r) {
    _touch();
    String h = _head("LED Settings", "/settings/led");
    h += "<form action='/save/led' method='POST'>";
    h += "<div class='card'><div class='card-h'>FRONT LEDs (ESP ×16)</div><div class='card-b'>";
    h += "<div class='row'><span class='lbl'>Enabled</span>"
         "<select name='fen'><option value='1'" + String(gLedSett.frontEnabled?" selected":"") + ">ON</option>"
         "<option value='0'" + String(!gLedSett.frontEnabled?" selected":"") + ">OFF</option></select></div>";
    h += "<div class='row'><span class='lbl'>DRL type</span>"
         "<select name='ftp'>"
         "<option value='0'" + String(gLedSett.frontType==0?" selected":"") + ">Static</option>"
         "<option value='1'" + String(gLedSett.frontType==1?" selected":"") + ">Breathing</option>"
         "<option value='2'" + String(gLedSett.frontType==2?" selected":"") + ">Chase</option>"
         "</select></div>";
    h += "<div class='row'><span class='lbl'>Color</span>" + _palSelect("fcl",gLedSett.frontColor) + "</div>";
    h += "<div class='row'><span class='lbl'>Turn speed (ms)</span>"
         "<input type='number' name='fsp' min='60' max='500' step='10' value='"
         + String(gLedSett.frontSpeedMs) + "'></div>";
    h += "</div></div>";
    h += "<div class='card'><div class='card-h'>REAR LEDs (NRF ×24) — DISABLED</div>"
         "<div class='card-b'><div class='row'><span class='lbl' style='color:#4a5568'>"
         "NRF module not active. Settings will apply when NRF is enabled.</span></div></div></div>";
    h += "<button class='btn btn-o' type='submit'>SAVE</button></form>" + _foot();
    r->send(200,"text/html",h);
  }

  void _saveLED(AsyncWebServerRequest *r) {
    _touch();
    if (r->hasParam("fen",true))  gLedSett.frontEnabled  = r->getParam("fen",true)->value()=="1";
    if (r->hasParam("ftp",true))  gLedSett.frontType     = (LedDrlType)r->getParam("ftp",true)->value().toInt();
    if (r->hasParam("fcl",true))  gLedSett.frontColor    = r->getParam("fcl",true)->value().toInt();
    if (r->hasParam("fsp",true))  gLedSett.frontSpeedMs  = r->getParam("fsp",true)->value().toInt();
    saveSettings(gSett,gLedSett,gTimeSett,gHRSett,gPrefs);
    r->redirect("/settings/led");
  }

  // ── /settings/gps — read only ─────────────────────────────
  void _handleGPS(AsyncWebServerRequest *r) {
    _touch();
    extern float gLat,gLon,gAltM,gSpeedKmh,gCourseD;
    extern int   gSatCount; extern bool gGpsFixed;
    String h = _head("GPS Info", "/settings/gps");
    h += "<div class='card'><div class='card-h'>GPS STATUS</div><div class='card-b'>";
    h += _row("Fix", gGpsFixed?"3D":"NO FIX", gGpsFixed?"g":"r");
    h += _row("Satellites", String(gSatCount), "y");
    h += _row("Latitude",   String(gLat,6)+"°");
    h += _row("Longitude",  String(gLon,6)+"°");
    h += _row("Altitude",   String(gAltM,0)+" m");
    h += _row("Speed",      String(gSpeedKmh,1)+" km/h");
    h += _row("Course",     String(gCourseD,1)+"°");
    h += _row("Module",     "ATGM336H");
    h += "</div></div>" + _foot();
    r->send(200,"text/html",h);
  }

  // ── /settings/imu ─────────────────────────────────────────
  void _handleIMU(AsyncWebServerRequest *r) {
    _touch();
    extern MPU6500Driver gIMU;
    String h = _head("IMU (MPU6500)", "/settings/imu");
    h += "<div class='card'><div class='card-h'>ACCELEROMETER</div><div class='card-b'>";
    h += _row("AX",String(gIMUSnapshot.ax,3)+" g");
    h += _row("AY",String(gIMUSnapshot.ay,3)+" g");
    h += _row("AZ",String(gIMUSnapshot.az,3)+" g");
    h += _row("Pitch",String(gIMUSnapshot.pitch,1)+"°");
    h += _row("Roll",String(gIMUSnapshot.roll,1)+"°");
    h += _row("Temp",String(gIMUSnapshot.tempC,1)+"°C");
    h += _row("Brake intensity",String((int)(gIMUSnapshot.brakeIntensity*100))+"%",
              gIMUSnapshot.isBraking?"r":"val");
    h += _row("Pothole count",String(gIMUSnapshot.potholeCount),"o");
    h += _row("Vib score",String(gIMUSnapshot.vibrationRMS,1)+"/10");
    h += _row("IMU OK",gIMU.isOK()?"YES":"NO",gIMU.isOK()?"g":"r");
    h += "</div></div>";
    h += "<div class='card'><div class='card-h'>CALIBRATION</div><div class='card-b'>";
    h += "<form action='/save/imu' method='POST'>";
    h += "<div class='row'><span class='lbl'>Brake threshold (g)</span>"
         "<input type='number' name='bthr' min='-1.0' max='-0.1' step='0.05' value='"
         + String(BRAKE_THRESHOLD,2) + "'></div>";
    h += "<div class='row'><span class='lbl'>Brake max (g)</span>"
         "<input type='number' name='bmax' min='-2.0' max='-0.3' step='0.05' value='"
         + String(BRAKE_MAX,2) + "'></div>";
    h += "<button class='btn btn-o' type='submit'>SAVE</button></form><br>";
    h += "<form action='/calibrate/imu' method='POST'>"
         "<button class='btn btn-g' type='submit'>CALIBRATE ZERO (bike upright)</button></form>";
    h += "</div></div>" + _foot();
    r->send(200,"text/html",h);
  }

  // ── /settings/battery ─────────────────────────────────────
  void _handleBattery(AsyncWebServerRequest *r) {
    _touch();
    String h = _head("Battery & System", "/settings/battery");
    h += "<div class='card'><div class='card-h'>ESP BATTERY</div><div class='card-b'>";
    extern float gBattVolt;
    h += _row("Voltage", String(gBattVolt,2)+" V", "y");
    h += _row("Level",   String((int)gBattPct)+"%",gBattPct>50?"g":gBattPct>20?"y":"r");
    h += _row("Config",  "3000 mAh (2P)");
    h += "</div></div>";
    h += "<div class='card'><div class='card-h'>ESP SYSTEM</div><div class='card-b'>";
    h += _row("Free heap",  String(ESP.getFreeHeap()/1024)+" KB","c");
    h += _row("PSRAM free", psramFound()?String(ESP.getFreePsram()/1024)+" KB":"N/A","c");
    h += _row("Flash used", String((ESP.getSketchSize())/1024)+" KB / "+String(ESP.getFlashChipSize()/1024/1024)+" MB");
    h += _row("Uptime",     String(millis()/1000)+" s");
    h += _row("Die temp",   String(temperatureRead(),1)+"°C","o");
    h += _row("Chip rev",   String(ESP.getChipRevision()));
    h += "</div></div>";
    h += "<div class='card'><div class='card-h'>NRF BATTERY (DISABLED)</div>"
         "<div class='card-b'><div class='row'><span class='lbl' style='color:#4a5568'>NRF module not connected</span></div></div></div>";
    h += "<div class='card'><div class='card-h'>ERRORS</div><div class='card-b'>";
    bool any=false;
    for (uint8_t i=0;i<ErrorHandler::MAX_ERRORS;i++) {
      auto &e=gErrors.entries()[i];
      if (e.active) {
        h += _row(ERR_NAMES[e.code], "count="+String(e.count)+" age="+String((millis()-e.firstMs)/1000)+"s","r");
        any=true;
      }
    }
    if (!any) h += _row("Status","All OK","g");
    h += "</div></div>" + _foot();
    r->send(200,"text/html",h);
  }

  // ── /settings/ble ─────────────────────────────────────────
  void _handleBLE(AsyncWebServerRequest *r) {
    _touch();
    extern BLEManager gBLE;
    String h = _head("BLE & WiFi", "/settings/ble");
    h += "<div class='card'><div class='card-h'>WiFi</div><div class='card-b'>";
    h += _row("WiFi MAC",    WiFi.macAddress().c_str(),"c");
    h += _row("OTA SSID",   OTA_SSID);
    h += _row("OTA IP",     "192.168.4.1");
    h += "</div></div>";
    h += "<div class='card'><div class='card-h'>BLE — Garmin Heart Rate</div><div class='card-b'>";
    h += _row("Status", gBLE.hrConnected()?"Connected":"Scanning...", gBLE.hrConnected()?"g":"y");
    h += _row("HR value", gHeartRateBpm>0?String(gHeartRateBpm)+" bpm":"--","pk");
    h += _row("Service", "GATT 0x180D (standard)");
    h += "</div></div>";
    h += "<div class='card'><div class='card-h'>BLE — NRF52840 Rear Unit (DISABLED)</div>"
         "<div class='card-b'><div class='row'><span class='lbl' style='color:#4a5568'>NRF module disabled in firmware</span></div></div></div>";
    h += _foot();
    r->send(200,"text/html",h);
  }

  // ── /settings/hr ──────────────────────────────────────────
  void _handleHR(AsyncWebServerRequest *r) {
    _touch();
    String h = _head("Heart Rate", "/settings/hr");
    h += "<div class='card'><div class='card-h'>LIVE</div><div class='card-b'>";
    h += _row("Current HR", gHeartRateBpm>0?String(gHeartRateBpm)+" bpm":"-- (no device)","pk");
    uint8_t mhr = gHRSett.maxHR;
    uint8_t z1=mhr*0.60f, z2=mhr*0.70f, z3=mhr*0.80f, z4=mhr*0.90f;
    h += "</div></div>";
    h += "<form action='/save/hr' method='POST'>";
    h += "<div class='card'><div class='card-h'>ZONES (based on Max HR)</div><div class='card-b'>";
    h += "<div class='row'><span class='lbl'>Max HR</span>"
         "<input type='number' name='mhr' min='120' max='220' value='"
         + String(mhr) + "'> bpm</div>";
    h += _row("Zone 1 Recovery", "< "+String(z1));
    h += _row("Zone 2 Aerobic",  String(z1)+"-"+String(z2));
    h += _row("Zone 3 Tempo",    String(z2)+"-"+String(z3));
    h += _row("Zone 4 Threshold",String(z3)+"-"+String(z4));
    h += _row("Zone 5 Anaerobic","> "+String(z4));
    h += "</div></div>";
    h += "<button class='btn btn-o' type='submit'>SAVE</button></form>";
    h += "<div class='info' style='margin-top:12px'>Source: Garmin Forerunner 255s via BLE GATT 0x180D<br>"
         "Activate on watch: Hold UP → Health &amp; Wellness → Wrist Heart Rate → Broadcast Heart Rate</div>";
    h += _foot();
    r->send(200,"text/html",h);
  }

  void _saveHR(AsyncWebServerRequest *r) {
    _touch();
    if (r->hasParam("mhr",true)) gHRSett.maxHR = r->getParam("mhr",true)->value().toInt();
    saveSettings(gSett,gLedSett,gTimeSett,gHRSett,gPrefs);
    r->redirect("/settings/hr");
  }

  // ── /settings/ota ─────────────────────────────────────────
  void _handleOTA(AsyncWebServerRequest *r) {
    _touch();
    String h = _head("OTA Update", "/settings/ota");
    h += "<div class='card'><div class='card-h'>FIRMWARE</div><div class='card-b'>";
    h += _row("Version",   FW_VERSION,"p");
    h += _row("Build",     FW_BUILD_DATE);
    h += _row("Flash free",String(ESP.getFreeSketchSpace()/1024)+" KB");
    h += "</div></div>";
    h += "<div class='card'><div class='card-h'>UPLOAD NEW FIRMWARE</div><div class='card-b'>";
    h += "<form id='upd' enctype='multipart/form-data'>"
         "<input type='file' id='f' name='firmware' accept='.bin' style='width:100%;padding:8px;"
         "background:#0d1117;border:1px solid #30363d;border-radius:6px;color:#e6edf3;margin-bottom:10px'>"
         "<button class='btn btn-p' type='submit'>UPLOAD .BIN</button></form>"
         "<div class='prog' id='pb'><div class='prog-bar' id='bar'></div></div>"
         "<div id='st' style='font-size:11px;color:#7d8590;margin-top:4px'>Ready</div>"
         "<script>"
         "document.getElementById('upd').addEventListener('submit',function(e){"
         "e.preventDefault();"
         "var f=document.getElementById('f').files[0];"
         "if(!f){alert('Select .bin file');return;}"
         "var fd=new FormData();"
         "fd.append('firmware',f);"
         "var x=new XMLHttpRequest();"
         "x.upload.onprogress=function(e){"
         "if(e.lengthComputable){"
         "var p=Math.round(e.loaded/e.total*100);"
         "document.getElementById('bar').style.width=p+'%';"
         "document.getElementById('st').textContent='Uploading: '+p+'%';}};"
         "x.onload=function(){"
         "if(x.status==200){document.getElementById('st').textContent='Done! Rebooting...';}"
         "else{document.getElementById('st').textContent='Error: '+x.responseText;}};"
         "x.open('POST','/update');x.send(fd);});"
         "</script>";
    h += "</div></div>" + _foot();
    r->send(200,"text/html",h);
  }

  void _handleUpdate(AsyncWebServerRequest *r, const String &fn,
      size_t idx, uint8_t *data, size_t len, bool final) {
    _touch();
    if (idx==0) {
      _written=0; _upMs=millis();
      DBG("OTA upload: %s size=%u", fn.c_str(), r->contentLength());
      // v1.1.2: Final force-save of odometer right before flash starts.
      // After Update.begin() the second OTA slot will be overwritten and
      // a crash here will reboot the device. NVS lives in its own partition
      // and is unaffected, so this save is durable.
      extern float gTotalDistKm;
      extern float gLastSavedKm;
      extern Preferences gPrefs;
      gPrefs.begin(PREF_BIKE, false);
      gPrefs.putFloat(PREF_TOTAL, gTotalDistKm);
      gPrefs.end();
      gLastSavedKm = gTotalDistKm;
      DBG("OTA: pre-flash odometer save: %.2f km", gTotalDistKm);

      if (!Update.begin(r->contentLength(),U_FLASH)) {
        DBG("OTA begin error: %s", Update.errorString());
        gErrors.set(ERR_OTA_FAIL, SEV_CRITICAL);
      }
    }
    if (Update.write(data,len)!=len) gErrors.set(ERR_OTA_FAIL, SEV_CRITICAL);
    _written+=len;
    size_t tot=r->contentLength();
    if (tot) progress=(uint8_t)(_written*100/tot);
    if (final) {
      if (!Update.end(true)) gErrors.set(ERR_OTA_FAIL, SEV_CRITICAL);
      else { DBG("OTA: flash OK"); progress=100; }
    }
  }

  // ── /settings/odometer ────────────────────────────────────
  void _handleOdometer(AsyncWebServerRequest *r) {
    _touch();
    String h = _head("Odometer", "/settings/odometer");
    h += "<div class='card'><div class='card-h'>DISTANCES</div><div class='card-b'>";
    h += _row("Total distance", String(gTotalDistKm,1)+" km","y");
    h += _row("Session",        String(gSessionDistKm,2)+" km","c");
    h += "</div></div>";
    h += "<div class='card'><div class='card-h'>ACTIONS</div><div class='card-b'>";
    h += "<form action='/reset/session' method='POST' style='margin-bottom:8px'>"
         "<button class='btn btn-o' type='submit'>RESET SESSION</button></form>";
    h += "<form action='/reset/total' method='POST'>"
         "<button class='btn btn-r' type='submit' onclick=\"return confirm('Reset total odometer?')\">"
         "RESET TOTAL ODOMETER</button></form>";
    h += "</div></div>" + _foot();
    r->send(200,"text/html",h);
  }

  // ── Register all routes ───────────────────────────────────
  void _setupRoutes() {
    _server.on("/",HTTP_GET,[this](auto*r){_handleRoot(r);});
    _server.on("/settings/display",HTTP_GET,[this](auto*r){_handleDisplay(r);});
    _server.on("/save/display",HTTP_POST,[this](auto*r){_saveDisplay(r);});
    _server.on("/settings/time",HTTP_GET,[this](auto*r){_handleTime(r);});
    _server.on("/save/time",HTTP_POST,[this](auto*r){_saveTime(r);});
    _server.on("/settings/led",HTTP_GET,[this](auto*r){_handleLED(r);});
    _server.on("/save/led",HTTP_POST,[this](auto*r){_saveLED(r);});
    _server.on("/settings/gps",HTTP_GET,[this](auto*r){_handleGPS(r);});
    _server.on("/settings/imu",HTTP_GET,[this](auto*r){_handleIMU(r);});
    _server.on("/settings/battery",HTTP_GET,[this](auto*r){_handleBattery(r);});
    _server.on("/settings/ble",HTTP_GET,[this](auto*r){_handleBLE(r);});
    _server.on("/settings/hr",HTTP_GET,[this](auto*r){_handleHR(r);});
    _server.on("/save/hr",HTTP_POST,[this](auto*r){_saveHR(r);});
    _server.on("/settings/ota",HTTP_GET,[this](auto*r){_handleOTA(r);});
    _server.on("/settings/odometer",HTTP_GET,[this](auto*r){_handleOdometer(r);});
    _server.on("/errors",HTTP_GET,[this](auto*r){_handleErrors(r);});
    _server.on("/reset/session",HTTP_POST,[this](auto*r){
      _touch();
      extern void resetSession();
      resetSession();
      r->redirect("/settings/odometer");});
    _server.on("/reset/total",HTTP_POST,[this](auto*r){
      _touch();
      extern void resetSession();
      gTotalDistKm=0;
      resetSession();
      gPrefs.begin(PREF_BIKE,false); gPrefs.putFloat(PREF_TOTAL,0); gPrefs.end();
      r->redirect("/settings/odometer");});
    _server.on("/calibrate/imu",HTTP_POST,[this](auto*r){
      _touch();
      // Set flag — main loop runs calibration in safe context
      extern volatile bool gCalibRequested;
      gCalibRequested = true;
      r->redirect("/settings/imu");});
    _server.on("/update",HTTP_POST,
      [this](auto*r){
        bool ok=!Update.hasError();
        r->send(ok?200:500,"text/plain",ok?"OK":Update.errorString());
        if(ok){delay(500);ESP.restart();}},
      [this](auto*r,auto fn,auto idx,auto d,auto len,auto fin){
        _handleUpdate(r,fn,idx,d,len,fin);});
    // Captive portal redirects
    auto captive=[](AsyncWebServerRequest*r){r->redirect("http://192.168.4.1/");};
    _server.on("/hotspot-detect.html",HTTP_GET,captive);
    _server.on("/generate_204",HTTP_GET,captive);
    _server.on("/connecttest.txt",HTTP_GET,captive);
    _server.onNotFound(captive);
  }
};

extern OtaManager gOTA;
