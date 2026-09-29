#ifndef POWERMANAGEMENT_H
#define POWERMANAGEMENT_H
#include <Arduino.h>

#define EE_MAGIC 0x4D315234UL       // "M1R4"
#define EE_ADDR  0

// 20 bytes. AVR has no struct alignment padding, so sizeof() is exactly the
// sum of the members and the CRC covers the first 18.
struct MeterRecord {
    uint32_t magic;
    int32_t  balance_mp;
    int32_t  tariff_mp;
    uint32_t lastTxId;
    uint8_t  sf;
    uint8_t  pad;
    uint16_t crc;
};

uint16_t crc16(const uint8_t* d, uint16_t n);

void setRelay(bool closed);
bool saveMeter(bool quiet);
void checkpointIfNeeded();
bool loadMeter(bool &factoryReset);

void runGaspISR();
void handleGasp();

#endif