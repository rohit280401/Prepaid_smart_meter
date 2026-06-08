#include "lora_ops.h"
#include "config.h"
#include "globals.h"
#include <LoRa.h>
#include <Arduino.h>
char payload[100]; // Increased buffer size slightly to accommodate UID safely

void createPayload() {
  char v[10], i[10], pf[10], bal[10];
  const char* cardUID = "C3BFDD34"; // Added card UID reference
  
  dtostrf(Voltage, 5, 2, v);
  dtostrf(Current, 5, 2, i);
  dtostrf(PF, 5, 2, pf);
  dtostrf(energyBalance, 5, 2, bal);

  // Appended UID to the front of telemetry metrics string
  sprintf(payload, "<Node1;UID:%s;V:%s;I:%s;PF:%s;Bal:%s>", cardUID, v, i, pf, bal);
}

void sendLoRa() {
  createPayload();
  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();
  Serial.println("Sent:");
  Serial.println(payload);
}