#include <SPI.h>
#include <MFRC522.h>
#include <EmonLib.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <LoRa.h>
#include <Arduino.h>

#include "config.h"
#include "globals.h"
#include "power_measure.h"
#include "rfid_ops.h"
#include "lora_ops.h"
#include "lcd_display.h"

// Objects
EnergyMonitor emon;
LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;


// Global variable definitions (shared across modules)
double energyBalance = 0;
bool cardLoaded = false;
bool loadState = false;
bool previousLoadState = false;
bool cardPresent = false;

double Voltage = 0, Current = 0, Power = 0, PF = 0;
unsigned long lastTime = 0;
unsigned long lastUpdate = 0;

void setup() {
  Serial.begin(9600);
  SPI.begin();

  // RFID
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);
  mfrc522.PCD_Init();
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  // LoRa
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init failed");
    while (1);
  }

  // Relay
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.print("Tap Card");

  // Energy monitor
  emon.voltage(VOLT_PIN, 417, -0.6);
  emon.current(CURR_PIN, 14);

  lastTime = millis();
}

void loop() {
  if (!cardLoaded) {
    readCardEnergy();
    delay(300);
    return;
  }

  processPower();

  if (cardPresent && energyBalance > 0) {
    loadState = true;
    digitalWrite(RELAY_PIN, LOW);
  } else {
    loadState = false;
    digitalWrite(RELAY_PIN, HIGH);
  }

  serialOutput();

  lcd_updateParameters();
  if (millis() - lastUpdate > 2000) {
    writeCardEnergy();
    sendLoRa();
    
    lastUpdate = millis();
  }

  previousLoadState = loadState;
}