#include <SPI.h>
#include <MFRC522.h>
#include <stdint.h>   // for fixed-width types

// Pin definitions (change if needed)
#define SS_PIN 8
#define RST_PIN 9

MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

// Memory block to use for storage (block 2 = first data block of sector 0)
// You can change this to any data block (e.g., 4 for sector 1)
const byte energyBlock = 2;

// Fixed-width type for energy (2 bytes). Change to uint32_t if you need larger values.
typedef uint16_t energy_t;

// Buffer for reading (must be at least 18 bytes)
byte buffer[18];
byte bufferSize = sizeof(buffer);

void setup() {
  Serial.begin(9600);
  while (!Serial);               // Wait for serial port (for boards with native USB)

  SPI.begin();                   // Initialize SPI bus
  mfrc522.PCD_Init();            // Initialize MFRC522

  Serial.println(F("\n=== RFID ENERGY MANAGER (CORRECTED) ==="));

  // --- Check communication with the RFID module ---
  byte version = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print(F("Firmware version: 0x"));
  Serial.print(version, HEX);
  if (version == 0x00 || version == 0xFF) {
    Serial.println(F(" -> ERROR: No communication with MFRC522. Check wiring!"));
    while (1);                     // Halt if no reader found
  } else if (version == 0x92 || version == 0x91) {
    Serial.println(F(" -> MFRC522 detected OK."));
  } else {
    Serial.println(F(" -> Unknown version, but likely working."));
  }

  // Prepare the default authentication key (all bytes 0xFF)
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  Serial.println(F("Place a Mifare Classic card on the reader..."));
  Serial.println();
}

void loop() {
  // Heartbeat – shows loop is running (every 5 seconds)
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 5000) {
    Serial.println(F("[Heartbeat] Waiting for card..."));
    lastHeartbeat = millis();
  }

  // --- Look for a new card ---
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;    // No card, go back and wait
  }

  // --- Select the card (read its serial number) ---
  if (!mfrc522.PICC_ReadCardSerial()) {
    Serial.println(F("Card detected but failed to read serial."));
    return;
  }

  // --- Card detected! Show UID ---
  Serial.println(F("\n*** Card Detected ***"));
  Serial.print(F("Card UID: "));
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) Serial.print("0");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
  }
  Serial.println();

  // --- Show card type (must be Mifare Classic) ---
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  Serial.print(F("Card type: "));
  Serial.println(mfrc522.PICC_GetTypeName(piccType));
  if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&
      piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
      piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
    Serial.println(F("ERROR: This card is not a Mifare Classic. Cannot read/write."));
    cleanup();
    return;
  }

  // --- Authenticate the block ---
  MFRC522::StatusCode status;
  status = mfrc522.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_A,
      energyBlock,
      &key,
      &(mfrc522.uid)
  );

  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Authentication failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    cleanup();
    return;
  }
  Serial.println(F("Authentication successful."));

  // --- Read current value from the block ---
  bufferSize = sizeof(buffer);   // Reset size before read
  status = mfrc522.MIFARE_Read(energyBlock, buffer, &bufferSize);

  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Read failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    Serial.println(F("Possible causes: block locked, wrong key, or card not Mifare Classic."));
    cleanup();
    return;
  }

  // Extract the stored energy value (first 2 bytes)
  energy_t currentEnergy;
  memcpy(&currentEnergy, buffer, sizeof(currentEnergy));
  Serial.print(F("Current energy units: "));
  Serial.println(currentEnergy);

  // --- Ask user for new value ---
  Serial.println(F("Enter new energy value (0-65535):"));

  // Wait up to 30 seconds for input
  unsigned long timeout = millis() + 30000;
  while (!Serial.available() && millis() < timeout) {
    // waiting
  }
  if (!Serial.available()) {
    Serial.println(F("No input received. Operation aborted."));
    cleanup();
    return;
  }

  energy_t newEnergy = (energy_t) Serial.parseInt();
  // Clear any leftover characters (newline, etc.)
  while (Serial.available()) Serial.read();

  Serial.print(F("New value entered: "));
  Serial.println(newEnergy);

  // --- Prepare write data (16 bytes, zero-filled, then copy new value) ---
  byte writeData[16] = {0};
  memcpy(writeData, &newEnergy, sizeof(newEnergy));

  // --- Write to the card ---
  status = mfrc522.MIFARE_Write(energyBlock, writeData, 16);

  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Write failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    cleanup();
    return;
  }

  Serial.println(F("Write successful!"));

  // --- Verify by reading back ---
  bufferSize = sizeof(buffer);
  status = mfrc522.MIFARE_Read(energyBlock, buffer, &bufferSize);

  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Verification read failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    cleanup();
    return;
  }

  energy_t storedEnergy;
  memcpy(&storedEnergy, buffer, sizeof(storedEnergy));
  Serial.print(F("Verified stored value: "));
  Serial.println(storedEnergy);

  Serial.println(F("--------------------------------\n"));

  // --- Clean up (halt card and stop encryption) ---
  cleanup();

  // Wait a moment before allowing next card
  delay(2000);
}

// Helper function to end communication with the card
void cleanup() {
  mfrc522.PICC_HaltA();            // Halt the card
  mfrc522.PCD_StopCrypto1();       // Stop encryption
}
