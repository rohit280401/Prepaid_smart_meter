#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <EmonLib.h>

// ============================================================================
// GLOBAL HARDWARE INSTANCES (LINKED ACROSS SCOPE)
// ============================================================================
extern EnergyMonitor emon;
extern LiquidCrystal_I2C lcd;
extern RTC_DS3231 rtc;

// ============================================================================
// DYNAMIC FINANCIAL & WALLET VARIABLES
// ============================================================================
extern volatile float accountBalance;       // Remaining prepaid wallet budget tracked in INR Rupees
extern volatile float tariffRate;           // Live utility rate calculated in INR Rupees per kWh
extern volatile uint32_t lastProcessedTxId; // Secure numerical ceiling filter to prevent double recharges

// ============================================================================
// REAL-TIME POWER SAMPLING VARIABLES
// ============================================================================
extern volatile float Voltage; // Measured Root Mean Square (RMS) line potential
extern volatile float Current; // Measured Root Mean Square (RMS) active load current
extern volatile float Power;   // Calculated Real Power currently drawn in Watts
extern volatile float PF;      // Current efficiency displacement factor (Power Factor)

// ============================================================================
// LOW-POWER SHADOW TIME KEEPING REGISTERS (STORED DIRECTLY IN RAM)
// ============================================================================
// These variables are updated once per second from the RTC via standard I2C.
// During an emergency power failure, the Dying Gasp ISR bypasses the I2C bus completely 
// and reads these RAM variables directly, cutting the RTC power profile to zero.
extern volatile uint8_t rtcHour;
extern volatile uint8_t rtcMinute;
extern volatile uint8_t rtcSecond;

// ============================================================================
// ASYNCHRONOUS STATE DRIVER FLAGS
// ============================================================================
extern volatile bool loadState;          // Operational state tracking for the house relay
extern volatile bool powerFailHandled;   // Noise-gate flag locking down the system during a Dying Gasp
extern volatile bool loraPacketReceived; // Asynchronous flag tripped by the LoRa hardware interrupt

// ============================================================================
// HIGH-ACCURACY TIME SYNC REGISTER
// ============================================================================
extern volatile unsigned long lastTime;  // Captures precision millis() timestamps for billing integration

#endif