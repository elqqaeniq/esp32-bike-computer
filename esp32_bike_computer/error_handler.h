#pragma once
// =============================================================
//  ERROR_HANDLER.H — Module error notifications
//  Shows alerts on display + Serial output
// =============================================================
#include <Arduino.h>

// Error codes
enum ErrorCode : uint8_t {
  ERR_NONE       = 0,
  ERR_DISPLAY    = 1,
  ERR_GPS        = 2,
  ERR_IMU        = 3,
  ERR_BTN        = 4,  // reserved (external button modules)
  ERR_VBAT_ADC   = 5,
  ERR_BLE_HR     = 6,
  ERR_FS         = 7,  // LittleFS mount/write failure (v1.1.2)
  ERR_OTA_FAIL   = 8,
  ERR_PSRAM      = 9,
  ERR_COUNT
};

static const char* ERR_NAMES[ERR_COUNT] = {
  "OK",
  "DISPLAY init failed",
  "GPS: no data (>5s)",
  "IMU: I2C not found (0x68)",
  "BTN: module error (reserved)",
  "Battery ADC: read error",
  "BLE HR: Garmin not found",
  "FS: LittleFS mount failed",
  "OTA: flash error",
  "PSRAM: not detected",
};

static const char* ERR_SHORT[ERR_COUNT] = {
  "OK","DISPLAY","NO GPS","NO IMU","BTN ERR",
  "BAT ERR","NO HR BLE","FS ERR","OTA ERR","NO PSRAM",
};

// Error severity
enum ErrSeverity { SEV_INFO, SEV_WARN, SEV_CRITICAL };

struct ErrorEntry {
  ErrorCode   code;
  ErrSeverity severity;
  uint32_t    firstMs;
  uint32_t    lastMs;
  uint16_t    count;
  bool        active;
};

class ErrorHandler {
public:
  static constexpr uint8_t MAX_ERRORS = 8;

  void set(ErrorCode code, ErrSeverity sev = SEV_WARN) {
    for (uint8_t i=0; i<MAX_ERRORS; i++) {
      if (_e[i].code == code) {
        _e[i].active = true;
        _e[i].lastMs = millis();
        _e[i].count++;
        _dirty = true;
        _printSerial(code, sev, true);
        return;
      }
    }
    // find free slot
    for (uint8_t i=0; i<MAX_ERRORS; i++) {
      if (!_e[i].active && _e[i].code == ERR_NONE) {
        _e[i] = {code, sev, millis(), millis(), 1, true};
        _dirty = true;
        _printSerial(code, sev, false);
        return;
      }
    }
  }

  void clear(ErrorCode code) {
    for (uint8_t i=0; i<MAX_ERRORS; i++) {
      if (_e[i].code == code) {
        _e[i].active = false;
        _dirty = true;
        Serial.printf("[ERR] CLEARED: %s\n", ERR_NAMES[code]);
        return;
      }
    }
  }

  bool hasActive() const {
    for (uint8_t i=0; i<MAX_ERRORS; i++)
      if (_e[i].active) return true;
    return false;
  }

  bool isActive(ErrorCode c) const {
    for (uint8_t i=0; i<MAX_ERRORS; i++)
      if (_e[i].code==c && _e[i].active) return true;
    return false;
  }

  // Get next active error for status bar icon
  ErrorCode firstActive() const {
    for (uint8_t i=0; i<MAX_ERRORS; i++)
      if (_e[i].active) return _e[i].code;
    return ERR_NONE;
  }

  // Consume dirty flag (for display redraw)
  bool dirty() { bool d=_dirty; _dirty=false; return d; }

  // For web settings page / serial dump
  void printAll() const {
    Serial.println("=== ERROR STATUS ===");
    bool any = false;
    for (uint8_t i=0; i<MAX_ERRORS; i++) {
      if (_e[i].active) {
        Serial.printf("  [%s] count=%u age=%lums\n",
          ERR_NAMES[_e[i].code], _e[i].count,
          millis()-_e[i].firstMs);
        any = true;
      }
    }
    if (!any) Serial.println("  All systems OK");
    Serial.println("====================");
  }

  // Returns JSON string for captive portal
  String toJSON() const {
    String j = "[";
    bool first = true;
    for (uint8_t i=0; i<MAX_ERRORS; i++) {
      if (_e[i].active) {
        if (!first) j += ",";
        j += "{\"code\":" + String(_e[i].code)
           + ",\"msg\":\"" + ERR_NAMES[_e[i].code]
           + "\",\"count\":" + String(_e[i].count)
           + ",\"age\":" + String((millis()-_e[i].firstMs)/1000)
           + "}";
        first = false;
      }
    }
    j += "]";
    return j;
  }

  const ErrorEntry* entries() const { return _e; }

private:
  ErrorEntry _e[MAX_ERRORS] = {};
  bool _dirty = false;

  void _printSerial(ErrorCode code, ErrSeverity sev, bool repeat) {
    const char* tag = sev==SEV_CRITICAL?"[CRITICAL]":
                      sev==SEV_WARN?"[WARN]":"[INFO]";
    if (repeat) {
      Serial.printf("%s REPEAT: %s\n", tag, ERR_NAMES[code]);
    } else {
      Serial.printf("%s NEW ERROR: %s\n", tag, ERR_NAMES[code]);
    }
  }
};

extern ErrorHandler gErrors;

// ── Module init check helpers ─────────────────────────────────
// Call these in setup() after each module init attempt

inline void checkModuleIMU(bool ok) {
  if (ok) {
    Serial.println("[INIT] IMU MPU6500: OK");
    gErrors.clear(ERR_IMU);
  } else {
    Serial.println("[INIT] IMU MPU6500: FAILED — check I2C wiring, addr 0x68");
    gErrors.set(ERR_IMU, SEV_CRITICAL);
  }
}

inline void checkModuleGPS(bool hasData) {
  if (hasData) {
    gErrors.clear(ERR_GPS);
  } else {
    gErrors.set(ERR_GPS, SEV_WARN);
  }
}

inline void checkModulePSRAM() {
  if (psramFound()) {
    Serial.printf("[INIT] PSRAM: %.1f MB available\n",
                  ESP.getFreePsram()/1048576.0f);
  } else {
    Serial.println("[INIT] PSRAM: NOT FOUND — track buffer will use heap");
    gErrors.set(ERR_PSRAM, SEV_WARN);
  }
}

inline void checkModuleBLEHR(bool connected) {
  if (connected) {
    Serial.println("[BLE] HR: Garmin connected");
    gErrors.clear(ERR_BLE_HR);
  } else {
    Serial.println("[BLE] HR: Garmin not found (scanning...)");
    gErrors.set(ERR_BLE_HR, SEV_INFO);
  }
}
