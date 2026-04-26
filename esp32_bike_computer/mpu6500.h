#pragma once
// =============================================================
//  MPU6500.H — IMU driver + brake/pothole/vibration detection
//  Axis: Y = forward, X = right, Z = up
// =============================================================
#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "error_handler.h"
#include <Preferences.h>

// MPU6500 registers
#define MPU_PWR_MGMT_1   0x6B
#define MPU_SMPLRT_DIV   0x19
#define MPU_CONFIG       0x1A
#define MPU_ACCEL_CFG    0x1C
#define MPU_GYRO_CFG     0x1B
#define MPU_ACCEL_XOUT_H 0x3B
#define MPU_WHO_AM_I     0x75
#define MPU_EXPECTED_ID  0x70   // MPU6500 = 0x70

struct IMUData {
  float ax, ay, az;          // g (calibrated)
  float gx, gy, gz;          // deg/s
  float pitch, roll;         // degrees
  float tempC;               // die temp
  float ayFiltered;          // LPF for braking
  float azFiltered;          // LPF for pothole
  float vibrationRMS;        // 0..10 score
  float brakeIntensity;      // 0.0..1.0
  bool  isBraking;
  uint16_t potholeCount;
  uint32_t lastPotholeMs;
  bool  ok;                  // I2C alive
};

class MPU6500Driver {
public:
  IMUData data = {};

  bool begin(float externalSpeedKmh_ref) {
    Wire.begin(PIN_IMU_SDA, PIN_IMU_SCL, IMU_I2C_FREQ);
    Wire.beginTransmission(IMU_ADDR);
    Wire.write(MPU_WHO_AM_I);
    Wire.endTransmission(false);
    Wire.requestFrom(IMU_ADDR, 1);
    if (!Wire.available()) { _ok=false; return false; }
    uint8_t id = Wire.read();
    if (id != MPU_EXPECTED_ID) {
      DBG("IMU: WHO_AM_I = 0x%02X, expected 0x%02X", id, MPU_EXPECTED_ID);
      _ok=false; return false;
    }
    _writeReg(MPU_PWR_MGMT_1, 0x01);   // wake, clock PLL
    delay(10);
    _writeReg(MPU_SMPLRT_DIV, 9);       // 1000/(9+1)=100 Hz internal
    _writeReg(MPU_CONFIG,  0x03);       // DLPF ~44Hz
    _writeReg(MPU_ACCEL_CFG, 0x00);     // ±2g
    _writeReg(MPU_GYRO_CFG,  0x00);     // ±250°/s
    _ok = true;
    _pSpeedRef = externalSpeedKmh_ref;  // (unused here, passed externally)
    DBG("IMU: MPU6500 init OK (id=0x%02X)", id);
    return true;
  }

  // Load calibration offsets from EEPROM
  void loadCalibration(Preferences &p) {
    p.begin(PREF_BIKE, true);
    _offAx = p.getFloat(PREF_CAL_AX, 0);
    _offAy = p.getFloat(PREF_CAL_AY, 0);
    _offAz = p.getFloat(PREF_CAL_AZ, 0);
    p.end();
    DBG("IMU: offsets ax=%.4f ay=%.4f az=%.4f", _offAx, _offAy, _offAz);
  }

  // Calibrate: collect N samples, compute offsets
  // Call when bike is stationary and level
  void calibrate(Preferences &p) {
    DBG("IMU: Calibrating (200 samples)...");
    float sumAx=0, sumAy=0, sumAz=0;
    const int N = 200;
    for (int i=0; i<N; i++) {
      float ax,ay,az,gx,gy,gz,t;
      _readRaw(ax,ay,az,gx,gy,gz,t);
      sumAx+=ax; sumAy+=ay; sumAz+=az;
      delay(10);
    }
    _offAx = sumAx/N;
    _offAy = sumAy/N;
    _offAz = sumAz/N - 1.0f; // remove 1g from Z (gravity)
    p.begin(PREF_BIKE, false);
    p.putFloat(PREF_CAL_AX, _offAx);
    p.putFloat(PREF_CAL_AY, _offAy);
    p.putFloat(PREF_CAL_AZ, _offAz);
    p.end();
    DBG("IMU: Cal done. offsets: ax=%.4f ay=%.4f az=%.4f", _offAx,_offAy,_offAz);
  }

  // Call at IMU_SAMPLE_HZ rate (use FreeRTOS task or timer)
  void sample(float speedKmh) {
    if (!_ok) { data.ok=false; return; }
    float ax,ay,az,gx,gy,gz,t;
    if (!_readRaw(ax,ay,az,gx,gy,gz,t)) { data.ok=false; return; }
    data.ok = true;

    // Apply calibration
    ax -= _offAx; ay -= _offAy; az -= _offAz;

    data.ax = ax; data.ay = ay; data.az = az;
    data.gx = gx; data.gy = gy; data.gz = gz;
    data.tempC = t;

    // Pitch/Roll from accelerometer
    data.pitch = atan2f(ay, sqrtf(ax*ax + az*az)) * RAD_TO_DEG;
    data.roll  = atan2f(-ax, az) * RAD_TO_DEG;

    // LPF
    data.ayFiltered = (1.0f-IMU_LPF_ALPHA)*data.ayFiltered + IMU_LPF_ALPHA*ay;
    data.azFiltered = (1.0f-IMU_LPF_ALPHA)*data.azFiltered + IMU_LPF_ALPHA*az;

    // Braking (Y-axis, only when moving)
    if (speedKmh > BRAKE_MIN_KMH && data.ayFiltered < BRAKE_THRESHOLD) {
      float raw = (data.ayFiltered - BRAKE_THRESHOLD) / (BRAKE_MAX - BRAKE_THRESHOLD);
      data.brakeIntensity = constrain(raw, 0.0f, 1.0f);
      data.isBraking = true;
    } else if (data.ayFiltered > BRAKE_THRESHOLD + BRAKE_HYSTERESIS) {
      data.isBraking = false;
      data.brakeIntensity = max(0.0f, data.brakeIntensity - 0.05f);
    }

    // Pothole detection (Z spike)
    uint32_t now = millis();
    float azNet = fabsf(az);
    if (azNet > POTHOLE_G && (now - data.lastPotholeMs) > POTHOLE_DEB_MS) {
      data.potholeCount++;
      data.lastPotholeMs = now;
      DBG("IMU: Pothole! count=%u az=%.2fg", data.potholeCount, azNet);
    }

    // Vibration RMS (running window via exponential)
    _rmsAccum = 0.95f*_rmsAccum + 0.05f*(az*az);
    data.vibrationRMS = constrain(sqrtf(_rmsAccum) * 5.0f, 0.0f, 10.0f);
  }

  bool isOK() const { return _ok; }

private:
  bool   _ok       = false;
  float  _offAx=0, _offAy=0, _offAz=0;
  float  _rmsAccum = 0;
  float  _pSpeedRef = 0; // unused here

  void _writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(IMU_ADDR);
    Wire.write(reg); Wire.write(val);
    Wire.endTransmission();
  }

  bool _readRaw(float &ax, float &ay, float &az,
                float &gx, float &gy, float &gz, float &t) {
    Wire.beginTransmission(IMU_ADDR);
    Wire.write(MPU_ACCEL_XOUT_H);
    Wire.endTransmission(false);
    Wire.requestFrom(IMU_ADDR, 14);
    if (Wire.available() < 14) return false;

    auto rd16 = [&]() -> int16_t {
      uint8_t h=Wire.read(), l=Wire.read();
      return (int16_t)((h<<8)|l);
    };
    ax = rd16() / 16384.0f;
    ay = rd16() / 16384.0f;
    az = rd16() / 16384.0f;
    int16_t rawT = rd16();
    t = rawT / 333.87f + 21.0f;
    gx = rd16() / 131.0f;
    gy = rd16() / 131.0f;
    gz = rd16() / 131.0f;
    return true;
  }
};

extern MPU6500Driver gIMU;
