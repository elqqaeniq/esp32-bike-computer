#pragma once
// =============================================================
//  ERROR_HANDLER.H v1.1.4 — Module error notifications
//  Shows alerts on display + Serial output
// =============================================================
#include <Arduino.h>

// Error codes
enum ErrorCode : uint8_t {
  ERR_NONE        = 0,
  ERR_DISPLAY     = 1,
  ERR_GPS         = 2,
  ERR_IMU         = 3,
  ERR_BTN         = 4,  // reserved (external button modules)
  ERR_VBAT_ADC    = 5,
  ERR_BLE_HR      = 6,
  ERR_FS          = 7,  // LittleFS mount/write failure (v1.1.2)
  ERR_OTA_FAIL    = 8,
  ERR_PSRAM       = 9,
  ERR_LAST_PANIC  = 10, // Previous boot ended in panic/WDT/brownout (v1.1.4)
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
  "Last boot: abnormal reset (see /crashes)",
};

static const char* ERR_SHORT[ERR_COUNT] = {
  "OK","DISPLAY","NO GPS","NO IMU","BTN ERR",
  "BAT ERR","NO HR BLE","FS ERR","OTA ERR","NO PSRAM","CRASHED",
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

  // Get first active error (for status bar dot / settings indicator).
  ErrorCode firstActive() const {
    for (uint8_t i=0; i<MAX_ERRORS; i++)
      if (_e[i].active) return _e[i].code;
    return ERR_NONE;
  }

  bool dirty() {
    bool d = _dirty;
    _dirty = false;
    return d;
  }

  const ErrorEntry* entries() const { return _e; }

private:
  ErrorEntry _e[MAX_ERRORS] = {};
  bool _dirty = false;

  void _printSerial(ErrorCode c, ErrSeverity sev, bool repeat) {
    const char* sevStr = sev==SEV_CRITICAL?"CRIT":sev==SEV_WARN?"WARN":"INFO";
    Serial.printf("[ERR][%s] %s %s\n", sevStr, ERR_NAMES[c],
                  repeat ? "(recurring)" : "(new)");
  }
};

// === Module-specific helpers (called from module code) =======
extern ErrorHandler gErrors;

inline void checkModuleBLEHR(bool ok) {
  if (ok) gErrors.clear(ERR_BLE_HR);
  else    gErrors.set(ERR_BLE_HR, SEV_WARN);
}

inline void checkModuleIMU(bool ok) {
  if (ok) gErrors.clear(ERR_IMU);
  else    gErrors.set(ERR_IMU, SEV_CRITICAL);
}
