#ifndef CONFIG_H
#define CONFIG_H
#include <Arduino.h>
// Pin definitions
#define RELAY_PIN   3
#define VOLT_PIN    A0
#define CURR_PIN    A1
#define SS_PIN      8
#define RST_PIN     9
#define LORA_SS     10
#define LORA_RST    7
#define LORA_DIO0   2

// RFID block for energy storage
const byte BLOCK_ADDR = 2;

// Authorized UID (replace with your card's UID)
extern const byte AUTHORIZED_UID[4];

// LoRa frequency (865 MHz for India, adjust as needed)
const long LORA_FREQ = 865E6;

#endif