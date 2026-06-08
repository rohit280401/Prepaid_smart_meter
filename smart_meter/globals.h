#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>

// Shared state variables
extern double energyBalance;
extern bool cardLoaded;
extern bool loadState;
extern bool previousLoadState;
extern bool cardPresent;

// Timekeeping
extern unsigned long lastTime;
extern unsigned long lastUpdate;

// Measurement values
extern double Voltage, Current, Power, PF;

#endif