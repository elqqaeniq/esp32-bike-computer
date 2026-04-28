#pragma once
// =============================================================
//  CONFIG.H — ESP32-S3-N16R8 Bike Computer
//  v1.1.4 — UI design tokens + crash log
//  NRF52840 parts: ALL COMMENTED OUT
// =============================================================

#define FW_VERSION    "1.1.4"
#define FW_BUILD_DATE __DATE__ " " __TIME__
#define DEVICE_NAME   "ESP32-BikeMain"

// --- Display ST77916 (QSPI 4-line) ---
// Module pinout: TE BL CS RST IO3 IO2 IO1 SDA SCL VCC GND
//
// QSPI uses 4 parallel data lines (D0..D3) instead of single MOSI:
//   SDA → D0   (SPI MOSI in single-line mode)
//   SCL → SCK  (clock)
//   IO1 → D1   (formerly DC pin in single-SPI displays — no DC in QSPI)
//   IO2 → D2
//   IO3 → D3
// Command/data distinction is encoded in the SPI header byte, not in a DC pin.
//
// TE (tearing effect) input — driven by display when frame is ready.
// Used as ISR-counter for FPS metric in v1.1.2.
// Full integration into draw loop deferred to v1.2 (with partial redraws).
// Power: 3.3V only (NOT 5V tolerant).
#define PIN_TFT_CS    10        // Chip select
#define PIN_TFT_SCK   12        // SCL — clock
#define PIN_TFT_D0    11        // SDA — data line 0 (was PIN_TFT_MOSI)
#define PIN_TFT_D1    13        // IO1 — data line 1 (was PIN_TFT_DC)
#define PIN_TFT_D2    15        // IO2 — data line 2 (new)
#define PIN_TFT_D3    38        // IO3 — data line 3 (new)
#define PIN_TFT_RST   14        // Reset
#define PIN_TFT_BL    21        // Backlight (PWM via LEDC)
#define PIN_TFT_TE    39        // Tearing-effect input (ISR-driven FPS counter, v1.1.2)
#define TFT_WIDTH     360
#define TFT_HEIGHT    360
#define TFT_SPI_FREQ  40000000UL  // QSPI 40MHz; reduces tearing (13ms/frame vs 26ms at 20MHz)
#define TFT_ROTATION  0
#define BL_LEDC_FREQ  5000
#define BL_LEDC_RES   8
// BL_LEDC_CH — NOT used in Core 3.x (ledcAttach(pin,freq,res) replaces ledcSetup+ledcAttachPin)

// --- GPS ATGM336H (UART1) ---
// Naming convention: from ESP32 perspective.
//   PIN_GPS_TX_PIN = ESP32 TX  → connects to GPS RX  (ESP sends commands to GPS)
//   PIN_GPS_RX_PIN = ESP32 RX  ← connects to GPS TX  (ESP receives NMEA from GPS)
// HardwareSerial::begin(baud, cfg, rxPin, txPin) — order matters!
#define PIN_GPS_TX_PIN    17     // ESP TX → GPS RX
#define PIN_GPS_RX_PIN    18     // ESP RX ← GPS TX
#define GPS_BAUD          9600
#define GPS_UART_NUM      1
#define GPS_UPDATE_MS     200
#define GPS_MIN_SATS      3
#define GPS_MAX_JUMP_KM   0.1f
#define GPS_SLOPE_WIN_M   15.0f

// --- IMU MPU6500 (I2C) ---
#define PIN_IMU_SDA       8
#define PIN_IMU_SCL       9
#define IMU_I2C_FREQ      400000
#define IMU_ADDR          0x68
#define IMU_SAMPLE_HZ     50
#define IMU_LPF_ALPHA     0.2f
#define BRAKE_THRESHOLD  -0.30f
#define BRAKE_MAX        -0.70f
#define BRAKE_HYSTERESIS  0.05f
#define BRAKE_MIN_KMH     3.0f
#define POTHOLE_G         3.0f
#define POTHOLE_DEB_MS    500

// --- WS2812B Front (GPIO16, 16 LEDs) ---
// GPIO48 = SPICLK_P_DIFF      → invalid for FastLED/RMT on ESP32-S3
// GPIO45 = VDD_SPI strapping   → invalid for FastLED/RMT on ESP32-S3
// GPIO16 = regular GPIO        → works with Adafruit_NeoPixel (any GPIO OK)
#define PIN_LED_DATA    16
#define NUM_LEDS_FRONT  16
#define LED_BRIGHT_DEF  120

// --- Encoder ---
#define PIN_ENC_CLK   4
#define PIN_ENC_DT    5
#define PIN_ENC_SW    6
#define ENC_DEB_MS    50
#define BTN_SHORT_MS  500
#define BTN_SETT_MS   5000

// --- Button inputs (external touch-button modules) ---
// External modules output HIGH when pressed, LOW when idle.
// No pull-up/pull-down needed on ESP — module handles it.
#define PIN_BTN1   1   // LEFT turn
#define PIN_BTN2   2   // RIGHT turn
#define PIN_BTN3   3   // THANK YOU / HAZARD long
#define BTN_LONG_MS 1500

// --- Battery ADC ---
#define PIN_VBAT      7
#define VBAT_RATIO    3.0f       // 200k+100k divider (v1.1.0 fix)
#define VBAT_MIN      3.30f
#define VBAT_MAX      4.20f
#define VBAT_SAMPLES  10
#define VBAT_INTERVAL_MS 5000

// --- BLE Garmin HR ---
#define BLE_HR_NAME   "Forerunner"   // matches Forerunner 255s, 245, etc
#define BLE_HR_SVC    "0000180d-0000-1000-8000-00805f9b34fb"
#define BLE_HR_CHAR   "00002a37-0000-1000-8000-00805f9b34fb"

// --- BLE NRF (DISABLED) ---
// #define BLE_NRF_NAME  "BikeRear-NRF"
// #define BLE_NRF_SVC   "..."
// #define BLE_NRF_LED   "..."
// #define BLE_NRF_BRAKE "..."
// #define BLE_NRF_CAD   "..."
// #define BLE_NRF_BATT  "..."

// --- BLE_DEBUG: extra DBG output in HR notify callback ---
// Comment out for release. Adds stack/heap watch in BLE task context.
// #define BLE_DEBUG 1

// --- OTA WiFi AP ---
#define OTA_SSID      "ESP32-Bike-OTA"
#define OTA_PASS      ""             // open AP for first setup
#define OTA_AP_IP     "192.168.4.1"
#define OTA_TIMEOUT_MS 600000UL      // 10 min idle → auto-stop

// --- Display update intervals ---
#define DISP_UPD_MS    100   // main screen redraw
#define SBAR_UPD_MS    500   // status bar redraw

// --- Odometer NVS ---
#define PREF_BIKE      "bike"
#define PREF_TOTAL     "total_km"
#define PREF_SETT      "settings"
#define PREF_THEME     "theme"
#define ODOM_SAVE_KM   1.0f

// =============================================================
//  UI DESIGN TOKENS (v1.1.4)
//  Single source of truth for canvas / color sentinels / radii.
//  Every new UI element should pull dimensions from here, not
//  invent its own constants.
// =============================================================

// Sentinel color for "transparent" canvas pixels.
// Magenta 0xF81F is loud, never used in our palette → safe sentinel.
// Used by drawArcLabel() and any future canvas-based UI element.
#define UI_SKIP_COLOR        0xF81F

// Status-bar arc geometry — single radius for all labels
#define UI_ARC_CX            180
#define UI_ARC_CY            180
#define UI_ARC_R_MID         165
#define UI_ARC_R_OUT         178   // outer edge of legacy ring (now unused, kept for ref)
#define UI_ARC_R_IN          152   // inner edge of legacy ring (now unused, kept for ref)

// Canvas dimensions for textSize 1 (6x8 chars)
// Use for indicators: BR, !FIX, FIX, sat count, turn arrows, error dot
#define UI_CVS_W_S1          60
#define UI_CVS_H_S1          12
#define UI_OUT_S1            68    // ceil(sqrt(60^2 + 12^2))

// Canvas dimensions for textSize 2 (12x16 chars)
// Use for primary info: time, HR, battery
#define UI_CVS_W_S2          96
#define UI_CVS_H_S2          20
#define UI_OUT_S2            98    // ceil(sqrt(96^2 + 20^2))

// Status bar bottom-arc rotation threshold
// Labels with angle in (90°..270°) get flipped 180° so they read
// right-side-up to a viewer looking at the front of the device.
#define UI_FLIP_LO_DEG       90.0f
#define UI_FLIP_HI_DEG       270.0f
