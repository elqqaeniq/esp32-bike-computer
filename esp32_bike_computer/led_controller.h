#pragma once
// =============================================================
//  LED_CONTROLLER.H — Front WS2812B (16 LEDs, ESP side)
//  Uses Adafruit_NeoPixel — no pin-validation issues on ESP32-S3.
//  Install: Arduino Library Manager → "Adafruit NeoPixel"
//  NRF rear LED commands: COMMENTED OUT
// =============================================================
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "ui_theme.h"

static const uint8_t TURN_PAT[16] = {1,1,1,1,0,0,1,0,0,1,0,0,1,1,1,1};

enum TurnState : uint8_t { TS_IDLE, TS_LEFT, TS_RIGHT, TS_THANKYOU, TS_HAZARD };

class LEDController {
public:
  TurnState turnState = TS_IDLE;

  void begin() {
    _strip.begin();
    _strip.setBrightness(LED_BRIGHT_DEF);
    _applyIdle();
    _strip.show();
  }

  void setBrightness(uint8_t pct) {
    _strip.setBrightness(map(pct, 0, 100, 0, 255));
  }

  void setLeft()  { turnState=TS_LEFT;     _offset=0; _sendNRFCmd(1); }
  void setRight() { turnState=TS_RIGHT;    _offset=0; _sendNRFCmd(2); }
  void setIdle()  { turnState=TS_IDLE;     _applyIdle(); _sendNRFCmd(0); }
  void setThankYou() {
    if (turnState==TS_THANKYOU) return;
    turnState=TS_THANKYOU;
    _tyCount=0; _tyTimer=millis(); _tyOn=false;
    _sendNRFCmd(3);
  }
  void setHazard() {
    turnState=TS_HAZARD;
    _offset=0; _sendNRFCmd(4);
  }

  void tick(uint32_t now, uint8_t frontColorIdx,
            bool frontDRLEnabled, uint8_t drlBrightPct) {
    switch(turnState) {
      case TS_IDLE:
        if (frontDRLEnabled) _applyDRL(frontColorIdx, drlBrightPct);
        else { _fillAll(0); _strip.show(); }
        break;
      case TS_LEFT:   _animTurn(false, now, frontColorIdx); break;
      case TS_RIGHT:  _animTurn(true,  now, frontColorIdx); break;
      case TS_HAZARD: _animHazard(now, frontColorIdx);      break;
      case TS_THANKYOU: _animThankYou(now, frontColorIdx);  break;
    }
  }

private:
  Adafruit_NeoPixel _strip{NUM_LEDS_FRONT, PIN_LED_DATA, NEO_GRB + NEO_KHZ800};

  uint8_t  _offset    = 0;
  uint32_t _lastAnim  = 0;
  uint16_t _animSpeed = LED_ANIM_MS;
  uint8_t  _tyCount   = 0;
  uint32_t _tyTimer   = 0;
  bool     _tyOn      = false;
  bool     _hazOn     = false;
  uint32_t _hazTimer  = 0;

  uint32_t _colorFromIdx(uint8_t idx) {
    RGB888 c = PAL888[idx % COL_COUNT];
    return _strip.Color(c.r, c.g, c.b);
  }

  uint32_t _scaleColor(uint32_t c, uint8_t scale) {
    uint8_t r = (uint8_t)((c >> 16) & 0xFF);
    uint8_t g = (uint8_t)((c >> 8)  & 0xFF);
    uint8_t b = (uint8_t)( c        & 0xFF);
    r = (uint16_t)r * scale / 255;
    g = (uint16_t)g * scale / 255;
    b = (uint16_t)b * scale / 255;
    return _strip.Color(r, g, b);
  }

  void _fillAll(uint32_t c) {
    for (uint16_t i = 0; i < NUM_LEDS_FRONT; i++) _strip.setPixelColor(i, c);
  }

  void _applyIdle() {
    _fillAll(0);
    _strip.show();
  }

  void _applyDRL(uint8_t colorIdx, uint8_t pct) {
    uint32_t c = _colorFromIdx(colorIdx);
    c = _scaleColor(c, map(pct, 0, 100, 0, 255));
    _fillAll(c);
    _strip.show();
  }

  void _animTurn(bool right, uint32_t now, uint8_t colorIdx) {
    if (now - _lastAnim < _animSpeed) return;
    _lastAnim = now;
    _offset = right ? (_offset+1)%16 : (_offset+15)%16;
    uint32_t c = _colorFromIdx(colorIdx);
    for (uint8_t i = 0; i < NUM_LEDS_FRONT; i++)
      _strip.setPixelColor(i, TURN_PAT[(_offset+i)%16] ? c : 0);
    _strip.show();
  }

  void _animThankYou(uint32_t now, uint8_t colorIdx) {
    if (_tyCount >= 4) { setIdle(); return; }
    if (now - _tyTimer < THANKYOU_MS) return;
    _tyTimer = now; _tyCount++; _tyOn = !_tyOn;
    _fillAll(_tyOn ? _colorFromIdx(colorIdx) : 0);
    _strip.show();
  }

  void _animHazard(uint32_t now, uint8_t colorIdx) {
    if (now - _hazTimer < THANKYOU_MS) return;
    _hazTimer = now; _hazOn = !_hazOn;
    _fillAll(_hazOn ? _colorFromIdx(colorIdx) : 0);
    _strip.show();
  }

  void _sendNRFCmd(uint8_t cmd) {
    (void)cmd;
    // NRF disabled: gBLE.sendLEDCmd(cmd);
    DBG("LED: cmd=0x%02X (NRF disabled)", cmd);
  }
};

extern LEDController gLED;
