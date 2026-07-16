#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <Arduino.h>

#define EEPROM_ADDR_BALANCE    0
#define EEPROM_ADDR_TXID       4
#define EEPROM_ADDR_TARIFF     8
#define EEPROM_ADDR_SF         12

void saveCurrentMeterToEEPROM(bool isEmergency);
bool recoverSavedMeterData(bool &powerOffDetected);
void runDyingGaspISR();
void handleDyingGasp();

#endif