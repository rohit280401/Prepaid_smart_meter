#ifndef ESP01_OPS_H
#define ESP01_OPS_H
#include <Arduino.h>

// Brings the ESP-01 up and joins the AP. Blocking, ~20 s. setup() only.
void esp01_init();

// Posts one telemetry record. Returns false if it was aborted because the
// dying-gasp ISR fired mid-transaction, true otherwise.
//
// NOTE: "true" means the bytes were pushed at the ESP, NOT that the server
// received them. No AT response is parsed. See BUILD_NOTES.
bool esp01_sendHttpTelemetry();

#endif