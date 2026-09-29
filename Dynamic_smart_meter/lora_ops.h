#ifndef LORA_OPS_H
#define LORA_OPS_H
#include <Arduino.h>

void applySF(uint8_t sf);
void sendAck(uint32_t txid, const char* status);
void handleIncoming();
bool sendTelemetry();

// True while telemetry is going out over Wi-Fi because LoRa stopped ACKing.
bool loraHttpMode();

#endif