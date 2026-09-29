#ifndef CONFIG_H
#define CONFIG_H
#include <Arduino.h>

#define NODE_ID     "Node1"
#define NODE_ID_LEN 5

// ============================================================================
//  BUILD KNOBS -- three independent switches. Turn things off here when flash
//  runs out, in this order: ENABLE_CONSOLE, then ENABLE_DEBUG, then
//  ESP_ON_HW_SERIAL (which also drops the SoftwareSerial library, ~1.2 KB).
// ============================================================================

//  0 = ESP-01 on SoftwareSerial pins 6/5. This is your proven configuration.
//      115200 works here because we only ever TRANSMIT: SoftwareSerial's TX
//      delay table is tuned for 115200 at 16 MHz. RECEIVING at 115200 on a
//      328P produces garbage, which is why no AT response is ever parsed.
//      If you want readable responses, set the ESP to 9600 once with
//      AT+UART_DEF=9600,8,1,0,0 and change ESP_BAUD below.
//  1 = ESP-01 on the hardware UART (D0/D1). Frees ~1.2 KB and makes the link
//      reliable in both directions, but kills the console and debug output,
//      and you must unplug ESP TX from D0 before every sketch upload.
#define ESP_ON_HW_SERIAL 0

#define ENABLE_DEBUG     1      // [TAG] prints on Serial. ~1.5-2.5 KB.
#define ENABLE_CONSOLE   0      // STATUS/CAL/TESTRECH/SETTIME. ~1.5-2.5 KB.
#define ESP_ECHO         1      // mirror ESP bytes to Serial while debugging
                                // (garbage at 115200 -- see note above)

#if ESP_ON_HW_SERIAL
  #undef  ENABLE_DEBUG
  #undef  ENABLE_CONSOLE
  #undef  ESP_ECHO
  #define ENABLE_DEBUG   0      // Serial IS the ESP. Debug would be AT noise.
  #define ENABLE_CONSOLE 0
  #define ESP_ECHO       0
  #define ESP_BAUD       115200
  #define SERIAL_BAUD    ESP_BAUD
#else
  #define ESP_BAUD       115200   // TX-only. Set to 9600 if you want RX too.
  #define SERIAL_BAUD    115200
#endif

#if ENABLE_DEBUG
  #define DBG(x)     Serial.print(x)
  #define DBGLN(x)   Serial.println(x)
  #define DBGF(x, d) Serial.print(x, d)
#else
  #define DBG(x)     do {} while (0)
  #define DBGLN(x)   do {} while (0)
  #define DBGF(x, d) do {} while (0)
#endif

// ============================================================================
//  PINS
// ============================================================================
#define RELAY_PIN       4
#define VOLT_PIN        A0      // ZMPT101B output
#define CURR_PIN        A1      // ACS712 output
#define LORA_SS         10
#define LORA_RST        7
#define LORA_DIO0       2
#define POWER_SENSE_PIN 3       // INT1. Mains-present detect.

#if !ESP_ON_HW_SERIAL
  #define ESP_RX_PIN    8      // <- ESP-01 TX
  #define ESP_TX_PIN    7       // -> ESP-01 RX (resistor divider, 3V3 logic!)
#endif

#define RELAY_CLOSED    LOW
#define RELAY_OPEN      HIGH

#define MAINS_PRESENT_LEVEL HIGH
#if MAINS_PRESENT_LEVEL == HIGH
  #define GASP_EDGE FALLING
#else
  #define GASP_EDGE RISING
#endif

// ============================================================================
//  LORA
// ============================================================================
const long LORA_FREQ = 865E6;
#define LORA_SYNC_WORD 0x3B
#define LORA_TX_POWER  17

#define SF_MIN     7
#define SF_MAX     12
#define SF_RESCUE  7

#define ACK_MISS_LIMIT 3
const uint16_t ACK_TIMEOUT_MS[6] PROGMEM = { 1200, 1600, 2400, 3600, 6000, 10000 };
#define TELEMETRY_INTERVAL_MS 15000UL

// ============================================================================
//  HTTP FALLBACK
// ============================================================================
//  The escalation ladder. On ACK_MISS_LIMIT consecutive misses:
//     currentSF <  SF_HTTP_THRESHOLD  ->  raise SF by SF_ESCALATE_STEP
//     currentSF >= SF_HTTP_THRESHOLD  ->  the link is dead, switch to HTTP
//
//  This is why the old `applySF(SF_RESCUE)` on link loss had to go: dropping
//  to SF 7 on failure made "SF >= 9 AND no ACK" permanently unreachable.
#define SF_HTTP_THRESHOLD 9
#define SF_ESCALATE_STEP  2

//  While in HTTP mode, retry LoRa on every Nth telemetry cycle. Any valid
//  received packet -- ACK or command -- returns the node to LoRa immediately.
#define HTTP_PROBE_EVERY  4

// --- Wi-Fi / Internet Fallback Credentials ---
#define WIFI_SSID       "LAPTOP"
#define WIFI_PASS       "Rohit2004"
#define HTTP_HOST       "192.168.137.1"
#define HTTP_PORT       1880
#define HTTP_PATH       "/sensor"

#define JSON_BUF_LEN    192     // worst case ~170 bytes. Static, not stack.

//  Trimmed hard from your bench sketch's ~21 s. Every millisecond spent here
//  is a millisecond the dying-gasp handler does not run. The POST also aborts
//  between steps if the gasp ISR fires. Total worst case ~4.5 s.
#define ESP_T_CIPSTART  2500
#define ESP_T_CIPSEND   600
#define ESP_T_BODY      1200
#define ESP_T_CIPCLOSE  500
#define ESP_T_ATCMD     400
#define ESP_T_JOIN      15000   // setup() only

// ============================================================================
//  SENSOR CALIBRATION & MONEY
// ============================================================================
#define VOLTAGE_CAL   180
#define VOLTAGE_PHASE 1.7
#define CURRENT_CAL   15.15

#define MIN_CURRENT_THRESHOLD 0.05
#define MAX_PLAUSIBLE_POWER_W 6000.0f

#define MP_PER_INR        100000L
#define BALANCE_MAX_MP    1000000000L
#define TARIFF_MIN_MP     (1L * MP_PER_INR / 10)    // INR 0.10 / kWh
#define TARIFF_MAX_MP     (50L * MP_PER_INR)        // INR 50.00 / kWh
#define TARIFF_DEFAULT_MP (8L * MP_PER_INR)

#define COST_DIVISOR      3600000000000LL
#define MAX_WINDOW_MS     10000UL

// ============================================================================
//  TIMING & DISPLAY
// ============================================================================
#define POWER_INTERVAL_MS 3000
#define LCD_INTERVAL_MS   3000
#define LCD_OVERLAY_MS    2500

#define CHECKPOINT_MIN_MS    300000UL
#define CHECKPOINT_MIN_DELTA (1L * MP_PER_INR)

#define GASP_CONFIRM_MS      30
#define GASP_SAMPLES         15

#define LCD_I2C_ADDR 0x27
#define LCD_COLS     16
#define LCD_ROWS     2

#define WATCHDOG_TIMEOUT WDTO_8S

#endif