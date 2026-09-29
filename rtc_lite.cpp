#include "rtc_lite.h"
#include "globals.h"
#include <Wire.h>

#define DS3231_ADDR   0x68
#define DS3231_STATUS 0x0F      // bit 7 = OSF (oscillator stop flag)

static bool lostPower = true;

static inline uint8_t b2d(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static inline uint8_t d2b(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

bool rtcLiteLostPower() { return lostPower; }

bool rtcLiteRead() {
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write((uint8_t)0x00);
    if (Wire.endTransmission() != 0) return false;

    // 16 bytes fits the 32-byte Wire buffer, so time + status in one go.
    if (Wire.requestFrom((uint8_t)DS3231_ADDR, (uint8_t)16) != 16) return false;

    uint8_t r[16];
    for (uint8_t i = 0; i < 16; i++) r[i] = (uint8_t)Wire.read();

    rtcSecond = b2d(r[0] & 0x7F);
    rtcMinute = b2d(r[1] & 0x7F);

    uint8_t h = r[2];
    if (h & 0x40) {                          // 12-hour mode
        uint8_t hh = b2d(h & 0x1F);
        if (h & 0x20) { if (hh != 12) hh = (uint8_t)(hh + 12); }
        else          { if (hh == 12) hh = 0; }
        rtcHour = hh;
    } else {                                 // 24-hour mode
        rtcHour = b2d(h & 0x3F);
    }

    rtcDay   = b2d(r[4] & 0x3F);
    rtcMonth = b2d(r[5] & 0x1F);
    rtcYear  = (uint16_t)(2000 + b2d(r[6]));

    lostPower = (r[DS3231_STATUS] & 0x80) != 0;
    return true;
}

bool rtcLiteSet(uint16_t Y, uint8_t Mo, uint8_t D,
                uint8_t H, uint8_t Mi, uint8_t S) {
    if (Y < 2000 || Y > 2099) return false;

    Wire.beginTransmission(DS3231_ADDR);
    Wire.write((uint8_t)0x00);
    Wire.write(d2b(S));
    Wire.write(d2b(Mi));
    Wire.write(d2b(H));            // bit 6 clear => 24-hour mode
    Wire.write((uint8_t)1);        // day-of-week, unused here
    Wire.write(d2b(D));
    Wire.write(d2b(Mo));           // bit 7 (century) left clear
    Wire.write(d2b((uint8_t)(Y - 2000)));
    if (Wire.endTransmission() != 0) return false;

    // Clear OSF so rtcTimeSet becomes true.
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write((uint8_t)DS3231_STATUS);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)DS3231_ADDR, (uint8_t)1) != 1) return false;
    uint8_t st = (uint8_t)Wire.read();

    Wire.beginTransmission(DS3231_ADDR);
    Wire.write((uint8_t)DS3231_STATUS);
    Wire.write((uint8_t)(st & 0x7F));
    if (Wire.endTransmission() != 0) return false;

    lostPower = false;
    return true;
}