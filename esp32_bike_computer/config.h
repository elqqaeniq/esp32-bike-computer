#pragma once
// =============================================================
//  CONFIG.H — ESP32-S3-N16R8 Bike Computer
//  NRF52840 parts: ALL COMMENTED OUT
// =============================================================

#define FW_VERSION    "1.1.3"
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
#define TFT_SPI_FREQ  20000000UL  // 20MHz for prototype wiring; restore to 40MHz on final PCB
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
#define PIN_BTN1      1       // Left turn
#define PIN_BTN2      2       // Right turn
#define PIN_BTN3      3       // ThankYou / Hazard
#define BTN_DEB_MS    80
#define BTN_LONG_MS   3000

// --- Battery ADC ---
// Divider: Vbat ─[R1=200k]─┬─[R2=100k]─GND   →  ADC = Vbat / 3
//                          │
//                          ├─[1kΩ]── PIN_VBAT (GPIO7)
//                          └─[100nF]─GND
// At Vbat=4.2V: ADC=1.40V (linear range, accurate)
// At Vbat=3.0V: ADC=1.00V
// VBAT_RATIO = (R1+R2)/R2 = 300/100 = 3.0
#define PIN_VBAT      7
#define VBAT_RATIO    3.0f
#define VBAT_FULL     4.20f
#define VBAT_EMPTY    3.00f
#define VBAT_SAMPLES  8

// --- BLE Garmin HR ---
#define BLE_HR_SVC    "0000180d-0000-1000-8000-00805f9b34fb"
#define BLE_HR_CHAR   "00002a37-0000-1000-8000-00805f9b34fb"
#define BLE_HR_NAME   "Forerunner"

// --- NRF BLE (ALL COMMENTED OUT) ---
// #define BLE_NRF_NAME       "BikeRearUnit"
// #define BLE_NRF_SVC        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
// #define BLE_CAD_CHAR       "beb5483e-36e1-4688-b7f5-ea07361b26a8"
// #define BLE_LED_CHAR       "beb5483e-36e1-4688-b7f5-ea07361b26a9"
// #define BLE_BRK_CHAR       "beb5483e-36e1-4688-b7f5-ea07361b26aa"
// #define BLE_NRFBAT_CHAR    "beb5483e-36e1-4688-b7f5-ea07361b26ab"
// #define LED_CMD_IDLE       0x00
// #define LED_CMD_LEFT       0x01
// #define LED_CMD_RIGHT      0x02
// #define LED_CMD_THANKYOU   0x03
// #define LED_CMD_HAZARD     0x04

// --- Preferences keys ---
#define PREF_BIKE     "bike"
#define PREF_THEME    "theme"
#define PREF_SETT     "settings"
#define PREF_TOTAL    "total_km"
#define PREF_CAL_AX   "cal_ax"
#define PREF_CAL_AY   "cal_ay"
#define PREF_CAL_AZ   "cal_az"
// Odometer save policy (NVS wear leveling):
//   ODOM_SAVE_KM    — minimum kilometers between periodic saves while riding.
//   Periodic save:  every ODOM_SAVE_KM (1.0km in v1.1.2, was 0.1km).
//   Forced save:    on settings menu open, OTA start, OTA flash final.
//   Rationale: 100k cycle/page NVS limit. With 0.1km step, 10 000km of riding
//   = 100k writes to the same key — at the wear limit. 1.0km gives 10x margin.
//   Trade-off: max ~1km lost on power-loss between saves (vs 100m before).
#define ODOM_SAVE_KM  1.0f

// --- Screens ---
#define SCR_RIDING    0
#define SCR_MAP       1
#define SCR_TERRAIN   2
#define SCR_COUNT     3
#define DISP_UPD_MS   400
#define SBAR_UPD_MS   1000
#define TRACK_MAX_PTS 2000
#define TRACK_MIN_M   5.0f

// --- OTA ---
#define OTA_SSID      "BikeComp_OTA"
#define OTA_PASS      "12345678"
#define OTA_TIMEOUT   300000UL

// --- LED timing ---
#define LED_ANIM_MS   120
#define THANKYOU_MS   300
#define SBAR_BLINK_HZ 2

// --- Serial ---
#define SERIAL_BAUD   115200
#define DBG(fmt,...) Serial.printf("[%lu] " fmt "\n", millis(), ##__VA_ARGS__)
