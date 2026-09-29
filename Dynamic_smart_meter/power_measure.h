#ifndef POWER_MEASURE_H
#define POWER_MEASURE_H
#include <Arduino.h>

// Samples the mains, deducts the cost of energy used since the last call, and
// drives the relay. All money arithmetic is integer milli-paisa.
void processPower();

#endif