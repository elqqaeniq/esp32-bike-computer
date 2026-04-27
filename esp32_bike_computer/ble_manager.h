#pragma once
// =============================================================
//  BLE_MANAGER.H — BLE Central: Garmin HR only
//  NRF52840 connection: ALL COMMENTED OUT
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
volatile uint16_t gHeartRateBpm = 0;  // Garmin HR
volatile uint8_t  gNrfBattPct   = 0;  // NRF battery (commented)

// ── HR parse ─────────────────────────────────────────────────
// GATT Heart Rate Measurement (0x2A37):
// Byte 0: flags (bit0=0 → HR is uint8, bit0=1 → uint16)
// Byte 1: HR value (or bytes 1-2 if uint16)
static void parseHRMeasurement(uint8_t* data, size_t len) {
  if (len < 2) return;
  bool is16bit = (data[0] & 0x01) != 0;
  if (is16bit && len >= 3)
    gHeartRateBpm = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
  else
    gHeartRateBpm = data[1];
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
// Forward-declared as a top-level class to avoid C++ restriction
// on defining new types in a new-type-id expression.
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

    // Timeout HR data (sensor may be off)
    if (_hrConnected && gHeartRateBpm > 0 && (now - _hrLastDataMs > 5000)) {
      DBG("BLE: HR data timeout (5s)");
      gHeartRateBpm = 0;
    }

    // ── NRF handling (ALL COMMENTED OUT) ───────────────────
    // if (_scanCB.foundNRF && !_nrfConnected) { _connectNRF(); }

    _scanning = _scan->isScanning();
  }

  bool hrConnected()  const { return _hrConnected; }
  // bool nrfConnected() const { return _nrfConnected; }  // NRF disabled

  // Stop BLE entirely — used before OTA AP start to free RAM
  // and avoid WiFi/BLE coexistence stress on ESP32-S3.
  void end() {
    if (_hrClient && _hrClient->isConnected()) _hrClient->disconnect();
    if (_scan) _scan->stop();
    BLEDevice::deinit(false);
    _hrConnected = false;
    _scanning    = false;
    gHeartRateBpm = 0;
    DBG("BLE: stopped (deinit)");
  }

  // Send LED command to NRF (COMMENTED OUT)
  // void sendLEDCmd(uint8_t cmd) {
  //   if (_nrfConnected && _ledChar) _ledChar->writeValue(&cmd,1,false);
  // }
  // void sendBrakeCmd(uint8_t intensity) {
  //   if (_nrfConnected && _brakeChar) _brakeChar->writeValue(&intensity,1,false);
  // }

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

  // ── Connect to Garmin HR ──────────────────────────────────
  static void _hrNotifyCB(BLERemoteCharacteristic*, uint8_t* d, size_t l, bool) {
    parseHRMeasurement(d, l);
    extern BLEManager gBLE;
    gBLE._hrLastDataMs = millis();
    checkModuleBLEHR(true);
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
      checkModuleBLEHR(true);
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
  checkModuleBLEHR(false);
}
