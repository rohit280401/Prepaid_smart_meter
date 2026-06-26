#ifndef POWER_MEASURE_H
#define POWER_MEASURE_H

// Reads voltage/current via EmonLib, computes power & power factor,
// updates availableUnits, and drives the relay.
void processPower();

// Prints current measurements to Serial for debugging.
void serialOutput();

#endif
