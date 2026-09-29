#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <EmonLib.h>

// RTClib is gone. It pulled in DateTime arithmetic, string formatting and
// TimeSpan for what amounted to reading six BCD registers. See rtc_lite.cpp.

extern EnergyMonitor     emon;
extern LiquidCrystal_I2C lcd;

// --- Money. Integer milli-paisa. 1 INR = 100,000 mp. ---
extern int32_t  balance_mp;
extern int32_t  tariff_mp;
extern uint32_t lastTxId;      // high-water mark: commands must EXCEED this

inline float   mpToInr(int32_t mp)  { return (float)mp / 100000.0f; }
inline int32_t inrToMp(float inr)   { return (int32_t)(inr * 100000.0f + 0.5f); }

// --- Link state ---
extern uint8_t currentSF;
extern uint8_t ackMisses;

// --- Measurements ---
extern float Voltage, Current, Power, PF;

// --- Shadow clock (RAM copy, so a wedged I2C bus cannot stall telemetry) ---
extern uint16_t rtcYear;
extern uint8_t  rtcMonth, rtcDay, rtcHour, rtcMinute, rtcSecond;
extern bool     rtcOk;        // I2C responds
extern bool     rtcTimeSet;   // ...and the oscillator never stopped

extern bool          loadState;
extern unsigned long lastBillMs;

// volatile: genuinely shared with the gasp ISR
extern volatile bool gaspLatched;
extern volatile bool gaspPending;

#endif