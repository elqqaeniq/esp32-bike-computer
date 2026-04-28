#pragma once
// =============================================================
//  BLE_MANAGER.H v1.1.4 — BLE Central: Garmin HR only
//  NRF52840 connection: ALL COMMENTED OUT
//
//  v1.1.4 CHANGES (BLE crash fix)
//  ------------------------------
//  Symptom: enabling Broadcast HR on Garmin → status bar / error
//  panel started flashing → ESP rebooted within seconds.
//
//  Root cause hypothesis (to be confirmed by /crashes log):
//    The HR notify callback runs in a NimBLE-internal task with
//    a small stack (~4 KB). Calling checkModuleBLEHR(true) from
//    the callback triggers gErrors.set/clear(), Serial.printf,
//    potentially NVS — risk of stack overflow or watchdog.
//
//  Fix:
//    1. _hrNotifyCB does only: parse + bump _hrLastDataMs.
//       checkModuleBLEHR is invoked from tick() instead.
//    2. Optional BLE_DEBUG: log stack/heap watermark on each
//       callback so we can confirm the stack-overflow theory.
//    3. gHeartRateBpm is now read via gHeartRateBpmSnap, refreshed
//       once per loop in main task — eliminates display flicker
//       from mid-frame value changes.
// =============================================================
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLERemoteCharacteristic.h>
#include "config.h"
#include "error_handler.h"

// ── Shared state ──────────────────────────────────────────────
volatile uint16_t gCadenceRpm   = 0;  // NRF cadence (commented)
volatile uint16_t gHeartRateBpm = 0;  // Garmin HR (raw, written by notify CB)
uint16_t          gHeartRateBpmSnap = 0;  // Safe-to-read snapshot, refreshed in main loop
volatile uint8_t  gNrfBattPct   = 0;  // NRF battery (commented)

// Refresh snapshot — call once per loop in main task.
inline void refreshHRSnapshot() {
  // 16-bit volatile load is atomic on Xtensa LX7
  gHeartRateBpmSnap = gHeartRateBpm;
}

// ── HR parse (fast, called from BLE callback context) ────────
// GATT Heart Rate Measurement (0x2A37):
//   Byte 0: flags (bit0=0 → uint8 HR, bit0=1 → uint16 HR)
//   Byte 1+: HR value
// Keep this minimal — no Serial, no allocations.
static inline void parseHR_fast(uint8_t* d, size_t l) {
  if (l < 2) return;
  uint16_t hr;
  if ((d[0] & 0x01) && l >= 3) {
    hr = (uint16_t)d[1] | ((uint16_t)d[2] << 8);
  } else {
    hr = d[1];
  }
  gHeartRateBpm = hr;  // single 16-bit atomic store
}

// ── BLE Scan Callback ─────────────────────────────────────────
class BLEScanCB : public BLEAdvertisedDeviceCallbacks {
public:
  BLEAdvertisedDevice* foundHR  = nullptr;
  // BLEAdvertisedDevice* foundNRF = nullptr;  // NRF disabled

  static BLEUUID hrSvcUUID;

  void onResult(BLEAdvertisedDevice dev) override {
    // --- Garmin HR ---
    if (!foundHR) {
      bool hasSvc  = dev.haveServiceUUID() && dev.isAdvertisingService(hrSvcUUID);
      bool hasName = dev.haveName() && String(dev.getName().c_str()).indexOf(BLE_HR_NAME) >= 0;
      if (hasSvc || hasName) {
        BLEDevice::getScan()->stop();
        foundHR = new BLEAdvertisedDevice(dev);
        DBG("BLE SCAN: Found HR device: %s", dev.getName().c_str());
      }
    }
    // --- NRF (COMMENTED OUT) ---
    // if (!foundNRF) {
    //   bool hasName = dev.haveName() && String(dev.getName().c_str()) == BLE_NRF_NAME;
    //   if (hasName) { foundNRF = new BLEAdvertisedDevice(dev); }
    // }
  }
};
BLEUUID BLEScanCB::hrSvcUUID = BLEUUID(BLE_HR_SVC);

// ── HR Client Callbacks (must be defined before BLEManager) ──
class HRClientCallbacks : public BLEClientCallbacks {
public:
  void onConnect(BLEClient*) override { }
  void onDisconnect(BLEClient*) override;  // defined after BLEManager
};

// ── BLE Manager class ─────────────────────────────────────────
class BLEManager {
public:
  // Call once in setup()
  void begin() {
    BLEDevice::init(DEVICE_NAME);
    _scan = BLEDevice::getScan();
    _scan->setAdvertisedDeviceCallbacks(&_scanCB);
    _scan->setInterval(1349);
    _scan->setWindow(449);
    _scan->setActiveScan(true);
    DBG("BLE: Initialized as Central");
    startScan();
  }

  void startScan() {
    DBG("BLE: Starting scan...");
    _scan->start(10, false);  // 10 sec, non-blocking
    _scanning = true;
  }

  // Call in loop()
  void tick() {
    uint32_t now = millis();

    // ── HR device handling ──────────────────────────────────
    if (_scanCB.foundHR && !_hrConnected) {
      _connectHR();
    }
    // Rescan requested by onDisconnect callback (never call BLE API from callback)
    if (_rescanPending && !_scanning) {
      _rescanPending = false;
      DBG("BLE: HR reconnect scan (deferred)");
      startScan();
    }
    // Reconnect if disconnected and no rescan scheduled
    if (!_hrConnected && !_scanning && !_rescanPending && (now - _hrDisconnectMs > 5000)) {
      DBG("BLE: HR reconnect scan");
      startScan();
    }

    // v1.1.4: error-flag bookkeeping moved here (was in notify CB).
    // Connected & data flowing → clear error.
    // Connected but stale > 5s   → set error (HR sensor turned off / belt fell off).
    if (_hrConnected) {
      uint32_t age = now - _hrLastDataMs;
      if (age < 5000) {
        // fresh data flowing — make sure error is cleared
        if (gErrors.isActive(ERR_BLE_HR)) checkModuleBLEHR(true);
      } else {
        // stale — drop reading and flag error
        if (gHeartRateBpm > 0) {
          DBG("BLE: HR data timeout (5s)");
          gHeartRateBpm = 0;
        }
        if (!gErrors.isActive(ERR_BLE_HR)) checkModuleBLEHR(false);
      }
    } else {
      // Not connected — ensure error reflects this
      if (!gErrors.isActive(ERR_BLE_HR)) checkModuleBLEHR(false);
    }

    // ── NRF handling (ALL COMMENTED OUT) ───────────────────
    // if (_scanCB.foundNRF && !_nrfConnected) { _connectNRF(); }

    _scanning = _scan->isScanning();
  }

  bool hrConnected()  const { return _hrConnected; }
  // bool nrfConnected() const { return _nrfConnected; }  // NRF disabled

  // For diagnostics: time since last HR notify (ms)
  uint32_t hrDataAge() const {
    return _hrLastDataMs == 0 ? UINT32_MAX : (millis() - _hrLastDataMs);
  }

  // Stop BLE entirely — used before OTA AP start to free RAM
  // and avoid WiFi/BLE coexistence stress on ESP32-S3.
  void end() {
    if (_hrClient && _hrClient->isConnected()) _hrClient->disconnect();
    if (_scan) _scan->stop();
    BLEDevice::deinit(false);
    _hrConnected = false;
    _scanning    = false;
    gHeartRateBpm = 0;
    gHeartRateBpmSnap = 0;
    DBG("BLE: stopped (deinit)");
  }

  // Send LED command to NRF (COMMENTED OUT)
  // void sendLEDCmd(uint8_t cmd) { ... }
  // void sendBrakeCmd(uint8_t intensity) { ... }

private:
  BLEScan*     _scan       = nullptr;
  BLEScanCB    _scanCB;
  bool         _scanning   = false;

  // HR (Garmin)
  BLEClient*               _hrClient       = nullptr;
  BLERemoteCharacteristic* _hrChar         = nullptr;
  volatile bool            _hrConnected    = false;
  volatile bool            _rescanPending  = false;  // set by onDisconnect, consumed by tick()
  volatile uint32_t        _hrDisconnectMs = 0;
  volatile uint32_t        _hrLastDataMs   = 0;

  // ── NRF (ALL COMMENTED OUT) ────────────────────────────────
  // BLEClient*               _nrfClient    = nullptr;
  // BLERemoteCharacteristic* _ledChar      = nullptr;
  // BLERemoteCharacteristic* _brakeChar    = nullptr;
  // bool                     _nrfConnected = false;

  friend class HRClientCallbacks;  // allow callback access to private fields

  // ── HR notify callback ────────────────────────────────────
  // CRITICAL: runs in NimBLE-internal task with ~4KB stack.
  // Do NOT call: gErrors.set/clear, Serial.printf in release,
  // NVS, malloc, anything that touches another mutex.
  // Just parse + bump timestamp. Error handling moves to tick().
  static void _hrNotifyCB(BLERemoteCharacteristic*, uint8_t* d, size_t l, bool) {
    extern BLEManager gBLE;
    gBLE._hrLastDataMs = millis();
    parseHR_fast(d, l);

#ifdef BLE_DEBUG
    // Stack/heap watchdog — only enabled in debug builds.
    // Min stack high-water mark observed → if it drops, we're close to overflow.
    UBaseType_t stack = uxTaskGetStackHighWaterMark(NULL);
    uint32_t heap = ESP.getFreeHeap();
    static UBaseType_t minStack = UINT32_MAX;
    static uint32_t   minHeap = UINT32_MAX;
    if (stack < minStack) minStack = stack;
    if (heap  < minHeap)  minHeap  = heap;
    // Rate-limit: log only every 10th call or when new minima
    static uint8_t cnt = 0;
    if (++cnt >= 10 || stack < 512 || heap < 20000) {
      cnt = 0;
      Serial.printf("[BLE_CB] hr=%u stack=%u(min=%u) heap=%u(min=%u)\n",
                    (unsigned)gHeartRateBpm,
                    (unsigned)stack, (unsigned)minStack,
                    (unsigned)heap,  (unsigned)minHeap);
    }
#endif
  }

  void _connectHR() {
    DBG("BLE: Connecting to HR device...");
    _hrClient = BLEDevice::createClient();

    _hrClient->setClientCallbacks(new HRClientCallbacks());

    if (!_hrClient->connect(_scanCB.foundHR)) {
      DBG("BLE: HR connect FAILED");
      delete _scanCB.foundHR; _scanCB.foundHR = nullptr;
      _hrDisconnectMs = millis();
      startScan();
      return;
    }

    BLERemoteService* svc = _hrClient->getService(BLEUUID(BLE_HR_SVC));
    if (!svc) {
      DBG("BLE: HR service not found");
      _hrClient->disconnect();
      return;
    }

    _hrChar = svc->getCharacteristic(BLEUUID(BLE_HR_CHAR));
    if (_hrChar && _hrChar->canNotify()) {
      _hrChar->registerForNotify(_hrNotifyCB);
      _hrConnected = true;
      _hrLastDataMs = millis();
      DBG("BLE: HR connected and subscribed!");
      // checkModuleBLEHR(true) → handled by tick() now
    }

    delete _scanCB.foundHR; _scanCB.foundHR = nullptr;
  }
};

extern BLEManager gBLE;

// ── HRClientCallbacks::onDisconnect — defined after BLEManager ─
inline void HRClientCallbacks::onDisconnect(BLEClient*) {
  gBLE._hrConnected    = false;
  gBLE._hrDisconnectMs = millis();
  gHeartRateBpm        = 0;
  gBLE._rescanPending  = true;  // tick() will call startScan() — never call BLE API from callback
  DBG("BLE: HR disconnected");
  // checkModuleBLEHR(false) → handled by tick() now
}
