#ifndef LORA_OPS_H
#define LORA_OPS_H

#include <Arduino.h>

// Function prototypes
void handleIncomingLoRaPacket();
void sendLoRaTelemetry();
void sendImmediateAck(uint32_t txid, const char* statusMsg);

#endif