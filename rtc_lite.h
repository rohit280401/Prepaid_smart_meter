#ifndef RTC_LITE_H
#define RTC_LITE_H
#include <Arduino.h>

// Minimal DS3231 driver. Replaces RTClib, which cost roughly 1.5-2 KB of flash
// to provide DateTime arithmetic this sketch never used -- the clock only ever
// feeds one LCD page.

// Reads regs 0x00..0x0F in one transaction, populates the rtc* globals and
// latches the oscillator-stop flag. Returns false if the chip does not ACK.
bool rtcLiteRead();

// True if the DS3231 has lost power since the clock was last set (OSF bit).
// This is a far better "clock not set" signal than guessing from the year.
bool rtcLiteLostPower();

// Writes the time and clears OSF. Returns false on I2C failure.
bool rtcLiteSet(uint16_t Y, uint8_t Mo, uint8_t D,
                uint8_t H, uint8_t Mi, uint8_t S);

#endif