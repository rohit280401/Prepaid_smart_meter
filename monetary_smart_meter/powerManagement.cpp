#include "powerManagement.h"
#include <EEPROM.h>
#include <SPI.h>        
#include <LoRa.h>       
#include "config.h"
#include "globals.h"

// Set distinct allocations inside internal non-volatile space to avoid data corruption
#define EEPROM_ADDR_BALANCE 0 // Takes up 4 bytes (Address 0, 1, 2, 3) for float cash tracking
#define EEPROM_ADDR_TXID    4 // Takes up 4 bytes (Address 4, 5, 6, 7) for uint32_t tracking

// ============================================================================
// SYSTEM MEMORY MIRRORING ROUTINE
// ============================================================================
void saveCurrentMeterToEEPROM(bool isEmergency) {
    // Write variables sequentially into non-volatile addresses
    EEPROM.put(EEPROM_ADDR_BALANCE, accountBalance);
    EEPROM.put(EEPROM_ADDR_TXID, lastProcessedTxId);

    if (!isEmergency) {
        Serial.print(F("[EEPROM] Synced Storage. Balance: ₹"));
        Serial.print(accountBalance, 2);
        Serial.print(F(" | Registered Transaction ID: "));
        Serial.println(lastProcessedTxId);
    }
}

// ============================================================================
// SYSTEM BOOT DATA RECOVERY CONFIGURATIONS
// ============================================================================
bool recoverSavedMeterData(bool &powerOffDetected) {
    powerOffDetected = false;
    float recoveredMoney = 0.0f;
    uint32_t recoveredTxId = 0;

    // Load values into temporary variables on reset
    EEPROM.get(EEPROM_ADDR_BALANCE, recoveredMoney);
    EEPROM.get(EEPROM_ADDR_TXID, recoveredTxId);

    // Sanity gate check: Handle uninitialized EEPROM cells or corrupt data
    if (isnan(recoveredMoney) || recoveredMoney < 0.0f || recoveredMoney > 100000.0f) {
        recoveredMoney = 0.0f;
    }
    
    // Pass temporary allocations into the active global memory variables
    accountBalance = recoveredMoney;
    lastProcessedTxId = recoveredTxId;

    Serial.println(F("\n====== STORAGE MEMORY RECOVERY ======"));
    Serial.print(F(" Active Wallet State : ₹")); Serial.println(accountBalance, 2);
    Serial.print(F(" Active Sequence ID  : "));   Serial.println(lastProcessedTxId);

    // Turn relay on immediately if funds are present
    if (accountBalance > 0.0f) {
        loadState = true;
        digitalWrite(RELAY_PIN, LOW); // Connect line load contacts
        Serial.println(F(" Output Relay State  : CONNECTED (GRID ONLINE)"));
        Serial.println(F("======================================"));
        return true;
    }

    loadState = false;
    digitalWrite(RELAY_PIN, HIGH); // Isolate grid
    Serial.println(F(" Output Relay State  : DISCONNECTED (EMPTY BALANCE)"));
    Serial.println(F("======================================"));
    return false;
}

// ============================================================================
// CRITICAL BLACKOUT PROTECTION SYSTEM: TRUE HARDWARE INTERRUPT (DYING GASP)
// ============================================================================
void runDyingGaspISR() {
    // Noise-gate filters: Ignore line bounce errors or repeat events
    if (powerFailHandled) return;
    if (digitalRead(POWER_SENSE_PIN) == HIGH) return; 

    powerFailHandled = true;

    // --- CRITICAL STEP 1: LOAD SHEDDING ---
    digitalWrite(RELAY_PIN, HIGH); // Disconnect load immediately to preserve capacitor charge

    // --- CRITICAL STEP 2: SECURE SYSTEM STATE DATA ---
    // Save remaining balance and transaction numbers to EEPROM before power drops (~15ms)
    EEPROM.put(EEPROM_ADDR_BALANCE, accountBalance);
    EEPROM.put(EEPROM_ADDR_TXID, lastProcessedTxId);

    // --- CRITICAL STEP 3: TRANSMIT LAST STATUS ALERT ---
    LoRa.beginPacket();
    LoRa.print(F("<BLACKOUT;Node1;BAL:"));
    LoRa.print(accountBalance, 2);
    LoRa.print(F(";LAST_ID:"));
    LoRa.print(lastProcessedTxId);
    LoRa.print(F(";TIME:"));

    // Prints time using your exact variables padded cleanly with leading zeros
    if (rtcHour < 10) LoRa.print('0');   LoRa.print(rtcHour);   LoRa.print(':');
    if (rtcMinute < 10) LoRa.print('0'); LoRa.print(rtcMinute); LoRa.print(':');
    if (rtcSecond < 10) LoRa.print('0'); LoRa.print(rtcSecond);

    LoRa.print(F(">"));
    LoRa.endPacket(); // Blocks execution until the hardware registers shift out completely

    // Force the transceiver into a deep sleep state to prevent brownout damage
    LoRa.sleep(); 
}