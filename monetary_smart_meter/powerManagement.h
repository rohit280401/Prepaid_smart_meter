#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <Arduino.h>

// ------------------------------------------------------------
// EEPROM Functions
// ------------------------------------------------------------
void saveCurrentMeterToEEPROM(bool isEmergency = false);

bool recoverSavedMeterData(bool &powerOffDetected);

void runDyingGaspISR();

#endif