#include "rfid_ops.h"
#include "config.h"
#include "globals.h"
#include "lcd_display.h"
#include <MFRC522.h>
#include <Arduino.h>
#include <LiquidCrystal_I2C.h> // Added for LCD support

extern MFRC522 mfrc522;      // defined in main .ino
extern MFRC522::MIFARE_Key key;
extern LiquidCrystal_I2C lcd; // Linked LCD object

const byte AUTHORIZED_UID[4] = {0xC3, 0xBF, 0xDD, 0x34};

void readCardEnergy() {
  byte atqa[2];
  byte atqaSize = sizeof(atqa);

  mfrc522.PICC_WakeupA(atqa, &atqaSize);

  if (!mfrc522.PICC_ReadCardSerial()) {
    cardPresent = false;
    return;
  }

  cardPresent = true;

  // Check if UID matches
  for (byte i = 0; i < 4; i++) {
    if (mfrc522.uid.uidByte[i] != AUTHORIZED_UID[i]) {
      Serial.println("Unauthorized card");
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("   WRONG CARD   ");
      lcd.setCursor(0, 1);
      lcd.print(" Please Try Again");
      
      delay(2500); // Give user time to read the screen
      lcd_showTapCard(); // Reset back to tap card prompt
      
      mfrc522.PICC_HaltA(); // Halt the wrong card
      return;
    }
  }

  MFRC522::StatusCode status;
  status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A,
                                    BLOCK_ADDR, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.println("Auth failed");
    return;
  }

  byte buffer[18];
  byte size = sizeof(buffer);
  status = mfrc522.MIFARE_Read(BLOCK_ADDR, buffer, &size);
  if (status == MFRC522::STATUS_OK) {
    uint16_t val;
    memcpy(&val, buffer, sizeof(val));
    energyBalance = val / 100.0;
    cardLoaded = true;
    Serial.print("Units Loaded: ");
    Serial.println(energyBalance);
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(" CARD ACCEPTED! ");
    delay(1500);
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

void writeCardEnergy() {
  uint16_t val = (uint16_t)(energyBalance * 100);
  byte dataBlock[16] = {0};
  memcpy(dataBlock, &val, sizeof(val));

  byte atqa[2];
  byte atqaSize = sizeof(atqa);
  mfrc522.PICC_WakeupA(atqa, &atqaSize);

  if (!mfrc522.PICC_ReadCardSerial()) {
    cardPresent = false;
    return;
  }

  cardPresent = true;

  MFRC522::StatusCode status;
  status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A,
                                    BLOCK_ADDR, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) return;

  status = mfrc522.MIFARE_Write(BLOCK_ADDR, dataBlock, 16);
  if (status == MFRC522::STATUS_OK) {
    Serial.print("Card Updated -> ");
    Serial.println(val);
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}