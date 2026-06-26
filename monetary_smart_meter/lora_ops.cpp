#include "lora_ops.h"
#include "config.h"
#include "globals.h"
#include "powerManagement.h"
#include "lcd_display.h"
#include <LoRa.h>
#include <Arduino.h>
#include "RTClib.h" 

#define NODE_ID "Node1" // Unique identification signature tag for this physical meter unit
static char payload[180];
extern RTC_DS3231 rtc;

// Forward declaration of the local confirmation handshake handler
void sendImmediateAck(uint32_t txid, const char* statusMsg);

// ============================================================================
// ASYNCHRONOUS TRANSACTION PARSING & RECHARGE PROCESSING
// ============================================================================
void handleIncomingLoRaPacket() {
    String incoming = "";
    
    // Extract everything from the physical FIFO buffer populated by the hardware event
    while (LoRa.available()) {
        incoming += (char)LoRa.read();
    }

    // Enforce protocol boundaries: Must wrap inside tags and target this specific meter Node ID
    if (incoming.startsWith("<") && incoming.endsWith(">") && incoming.indexOf(NODE_ID) != -1) {
        
        // --- Look for the mandatory Transaction Sequence ID (TXID:) ---
// --- Locate this block inside handleIncomingLoRaPacket() ---
            int txidIdx = incoming.indexOf("TXID:");
            if (txidIdx != -1) {
                int endTxIdx = incoming.indexOf(';', txidIdx);
                if (endTxIdx == -1) endTxIdx = incoming.indexOf('>', txidIdx);
                
                // Extract the substring containing the numeric characters
                String idString = incoming.substring(txidIdx + 5, endTxIdx);
                
                // FIX: Convert String to unsigned long integer using safe standard types
                uint32_t incomingTxId = (uint32_t)strtoul(idString.c_str(), NULL, 10);
            // ----------------------------------------------------------------
            // TRANSACTION ROUTINE 1: REMOTE OVER-THE-AIR RECHARGE COMMAND
            // ----------------------------------------------------------------
            int rechIdx = incoming.indexOf("RECH:");
            if (rechIdx != -1) {
                int endIdx = incoming.indexOf(';', rechIdx);
                if (endIdx == -1) endIdx = incoming.indexOf('>', rechIdx);
                
                float rechargeAmount = incoming.substring(rechIdx + 5, endIdx).toFloat();

                // IDEMPOTENCY SAFETY DEDUPLICATION CHECK:
                if (incomingTxId <= lastProcessedTxId) {
                    // This transaction was already processed! Do NOT credit money again.
                    // The gateway likely missed our previous ACK. Re-send it to resolve the loop.
                    Serial.print(F("[REPLAY ENFORCED] TXID: "));
                    Serial.print(incomingTxId);
                    Serial.println(F(" already written. Dropping action, re-sending ACK confirmation."));
                    
                    sendImmediateAck(incomingTxId, "RECH_OK");
                    LoRa.receive(); // Put radio back into continuous listening mode
                    return;
                }

                // If check passes, processing the transaction is safe
                if (rechargeAmount > 0.0f) {
                    accountBalance += rechargeAmount; // Add funds directly to the wallet balance in RAM
                    lastProcessedTxId = incomingTxId;  // Move tracking index up to the new ID location
                    
                    saveCurrentMeterToEEPROM(false);   // Sync both variables to internal EEPROM memory
                    
                    Serial.print(F("[TX SUCCESS] ID: "));
                    Serial.print(lastProcessedTxId);
                    Serial.print(F(" | Credited: ₹"));
                    Serial.println(rechargeAmount, 2);
                    
                    lcd_showCardAccepted(accountBalance); 
                    sendImmediateAck(lastProcessedTxId, "RECH_OK"); // Transmit validation receipt
                }
            }

            // ----------------------------------------------------------------
            // TRANSACTION ROUTINE 2: REMOTE LIVE TARIFF ADJUSTMENT COMMAND
            // ----------------------------------------------------------------
            int tariIdx = incoming.indexOf("TARI:");
            if (tariIdx != -1) {
                int endIdx = incoming.indexOf(';', tariIdx);
                if (endIdx == -1) endIdx = incoming.indexOf('>', tariIdx);
                
                float newRate = incoming.substring(tariIdx + 5, endIdx).toFloat();

                // Deduplication check for configuration variables
                if (incomingTxId <= lastProcessedTxId) {
                    sendImmediateAck(incomingTxId, "TARI_OK");
                    LoRa.receive();
                    return;
                }

                if (newRate > 0.0f) {
                    tariffRate = newRate;             // Dynamically overwrite live calculation scale parameters
                    lastProcessedTxId = incomingTxId; // Shift execution tracking threshold forward
                    saveCurrentMeterToEEPROM(false);  // Secure parameters to internal EEPROM
                    
                    Serial.print(F("[TARIFF MODIFIED] New Dynamic Scale Base: ₹"));
                    Serial.println(tariffRate, 2);
                    
                    sendImmediateAck(lastProcessedTxId, "TARI_OK");
                }
            }
        }
    }
    LoRa.receive(); // Always re-arm the transceiver for continuous asynchronous reception
}

// ============================================================================
// HIGH-PRIORITY HANDSHAKE ACKNOWLEDGEMENT TRANSMITTER
// ============================================================================
void sendImmediateAck(uint32_t txid, const char* statusMsg) {
    char ackPayload[64];
    // Formats a tracking response string: e.g. <ACK;Node1;TXID:1042;STATUS:RECH_OK>
    snprintf(ackPayload, sizeof(ackPayload), "<ACK;%s;TXID:%lu;STATUS:%s>", NODE_ID, txid, statusMsg);
    
    LoRa.beginPacket();
    LoRa.print(ackPayload);
    LoRa.endPacket(); // Synchronous push to complete transceiver operations immediately
    
    Serial.print(F("[UPLINK ACK] Handshake Complete: "));
    Serial.println(ackPayload);
}

// ============================================================================
// PERIODIC STATUS METRICS TELEMETRY REPORT TRANSMITTER
// ============================================================================
void sendLoRaTelemetry() {
  char v[16], i[16], pf[16], bal[16], p[16], rate[16];
  char dt_str[32]; 

  dtostrf(Voltage, 5, 2, v);
  dtostrf(Current, 5, 2, i);
  dtostrf(Power, 5, 2, p);
  dtostrf(PF, 5, 2, pf);
  dtostrf(accountBalance, 5, 2, bal);
  dtostrf(tariffRate, 4, 2, rate);   

  DateTime now = rtc.now();
  snprintf(dt_str, sizeof(dt_str), "%04d-%02d-%02d_%02d:%02d:%02d", 
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());

  // Package telemetry status parameters, including the last ID applied, for visibility on the dashboard
  snprintf(payload, sizeof(payload), "<%s;V:%s;I:%s;P:%s;PF:%s;BAL:%s;RATE:%s;LAST_ID:%lu;DT:%s>",
           NODE_ID, v, i, p, pf, bal, rate, lastProcessedTxId, dt_str);

  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();
  
  LoRa.receive(); // Explicitly re-open the listening window right after transmitting
}