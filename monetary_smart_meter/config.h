#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// HARDWARE PIN DEFINITIONS
// ============================================================================
#define RELAY_PIN       4   // Relay output controlling the consumer grid load path
#define VOLT_PIN        A0  // Analog pin monitoring mains voltage waveform via transformer
#define CURR_PIN        A1  // Analog pin monitoring mains current waveform via CT sensor

// --- LoRa Transceiver SPI & Interrupt Allocations ---
#define LORA_SS         10  // SPI Chip Select Pin for the SX1276 module
#define LORA_RST        7   // Physical Reset Pin for the SX1276 module
#define LORA_DIO0       2   // MUST BE PIN 2: Captures the RX-Done hardware interrupt event

// --- Dying Gasp Voltage Monitoring ---
#define POWER_SENSE_PIN 3   // MUST BE PIN 3: Tied to external interrupt 1 for blackout detection

// ============================================================================
// COMMUNICATION & CALIBRATION PARAMETERS
// ============================================================================
const long LORA_FREQ = 865E6; // Operating frequency set to 865 MHz (Licensed band for India)

// --- EmonLib Energy Measurement Calibration Coefficients ---
#define VOLTAGE_CAL           417.0 // Adjusts raw ADC values to match physical AC Voltage scale
#define VOLTAGE_PHASE         -0.6  // Eliminates phase shift delays caused by transformer windings
#define CURRENT_CAL           14.0  // Adjusts raw ADC values to match physical Amperage scale
#define MIN_CURRENT_THRESHOLD 0.05  // Noise floor filter (ignores small leakages below 50mA)

// --- Non-Blocking Background Execution Intervals (Milliseconds) ---
#define POWER_INTERVAL_MS  1000  // Run power calculations and energy consumption checks every 1s
#define LCD_INTERVAL_MS    1000  // Refresh physical user interface layout statistics every 1s
#define LORA_INTERVAL_MS   5000  // Periodically send a background status report packet upstream every 5s

// --- Liquid Crystal Interface Properties ---
#define LCD_I2C_ADDR 0x27  // Hardware I2C device address for standard 16x2 display module
#define LCD_COLS     16    // Layout matrix constraints (Width)
#define LCD_ROWS     2     // Layout matrix constraints (Height)

#endif