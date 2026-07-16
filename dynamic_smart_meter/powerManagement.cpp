#include "powerManagement.h"
#include <EEPROM.h>
#include <SPI.h>        
#include <LoRa.h>       
#include "config.h"
#include "globals.h"

// A single execution lock to guarantee we only execute the power down routine once
volatile bool powerFailHandled = false;   

// ============================================================================
// PURE HARDWARE INTERRUPT BLACKOUT DELIVERY SYSTEM (NO POLLING)
// ============================================================================
void runDyingGaspISR() {
    // Noise gate defense
    if (powerFailHandled) return;
    powerFailHandled = true;

    // STEP 1: Turn off the interrupt immediately! 
    // This stops electrical voltage bouncing on Pin 3 from recursively breaking the CPU.
    detachInterrupt(digitalPinToInterrupt(POWER_SENSE_PIN));

    // STEP 2: Shed the load instantly to save capacitor energy
    digitalWrite(RELAY_PIN, HIGH); 

    // STEP 3: The Secret Sauce. Re-enable global interrupts inside this ISR context.
    // This unblocks the timing clocks and allows the SPI bus to process data frames.
    interrupts(); 
        // STEP 5: Commit values to non-volatile EEPROM storage
    EEPROM.put(EEPROM_ADDR_BALANCE, accountBalance);

    // STEP 4: Transmit the emergency packet directly from the interrupt execution path
    LoRa.beginPacket();
    LoRa.print(F("<BLACKOUT;Node1;BAL:"));
    LoRa.print(accountBalance, 2);
    LoRa.print(F(";LAST_ID:"));
    LoRa.print(lastProcessedTxId);
    LoRa.print(F(";TIME:"));
    
    if (rtcHour < 10) LoRa.print('0');   LoRa.print(rtcHour);   LoRa.print(':');
    if (rtcMinute < 10) LoRa.print('0'); LoRa.print(rtcMinute); LoRa.print(':');
    if (rtcSecond < 10) LoRa.print('0'); LoRa.print(rtcSecond);
    
    LoRa.print(F(">"));
    LoRa.endPacket(); // This will no longer deadlock because interrupts() is active!
    LoRa.sleep();



    // STEP 6: Lock the processor completely. 
    // We never return to loop(). The device freezes here until it goes totally dark.
    while(1) {
        // Safe containment zone
    }
}

// Keep this empty function structure only if other files expect its declaration,
// otherwise it can be completely removed since no polling is happening anymore.
void handleDyingGasp() {
    // Left empty intentionally: Architecture is now completely interrupt-driven
}

// ============================================================================
// SYSTEM MEMORY MIRRORING ROUTINE (For standard runtime use)
// ============================================================================
void saveCurrentMeterToEEPROM(bool isEmergency) {
    EEPROM.put(EEPROM_ADDR_BALANCE, accountBalance);
    EEPROM.put(EEPROM_ADDR_TXID, lastProcessedTxId);
    EEPROM.put(EEPROM_ADDR_TARIFF, tariffRate);
    EEPROM.put(EEPROM_ADDR_SF, meterCurrentSF);

    if (!isEmergency) {
        Serial.print(F("[EEPROM] Synced Storage. Balance: ₹"));
        Serial.print(accountBalance, 2);
        Serial.print(F(" | TXID: "));
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
    float recoveredTariff = 0.0f;      
    uint8_t recoveredSF = 7;

    EEPROM.get(EEPROM_ADDR_BALANCE, recoveredMoney);
    EEPROM.get(EEPROM_ADDR_TXID, recoveredTxId);
    EEPROM.get(EEPROM_ADDR_TARIFF, recoveredTariff);  
    EEPROM.get(EEPROM_ADDR_SF, recoveredSF);

    if (isnan(recoveredMoney) || recoveredMoney <= 0.0f || recoveredMoney > 100000.0f) {
        recoveredMoney = 10.0f;
        EEPROM.put(EEPROM_ADDR_BALANCE, recoveredMoney);
    }

    if (isnan(recoveredTariff) || recoveredTariff <= 0.0f || recoveredTariff > 100.0f) {
        recoveredTariff = 8.0f;
        EEPROM.put(EEPROM_ADDR_TARIFF, recoveredTariff);
    }

    if (recoveredSF < 7 || recoveredSF > 12) {
        recoveredSF = 7;
        EEPROM.put(EEPROM_ADDR_SF, recoveredSF);
    }
    
    accountBalance = recoveredMoney;
    lastProcessedTxId = recoveredTxId;
    tariffRate = recoveredTariff;
    meterCurrentSF = recoveredSF;

    LoRa.setSpreadingFactor(meterCurrentSF);

    if (accountBalance > 0.0f) {
        loadState = true;
        digitalWrite(RELAY_PIN, LOW);
        return true;
    }

    loadState = false;
    digitalWrite(RELAY_PIN, HIGH);
    return false;
}