#include "lora_ops.h"
#include "config.h"
#include "globals.h"
#include "powerManagement.h"
#include "lcd_display.h"
#include <LoRa.h>
#include <Arduino.h>
#include "RTClib.h" 

#define NODE_ID "Node1" 
static char payload[180];
extern RTC_DS3231 rtc;

// ============================================================================
// GLOBAL VARIABLES (defined here, declared extern in globals.h)
// ============================================================================
uint8_t meterCurrentSF = 7;                    // Now defined here
unsigned long telemetryIntervalMs = 10000;     // Now defined here

bool waitingForEdgeAck = false; 
unsigned long loraTxTimestamp = 0; 

// ============================================================================
// FUNCTION IMPLEMENTATIONS
// ============================================================================
void handleIncomingLoRaPacket() {
    int packetSize = LoRa.parsePacket();
    if (packetSize == 0) {
        return; 
    }

    String incoming = "";
    while (LoRa.available()) {
        incoming += (char)LoRa.read();
    }
    incoming.trim();

    if (incoming.startsWith("<") && incoming.endsWith(">") && incoming.indexOf(NODE_ID) != -1) {
        
        Serial.print(F("RECEIVED: "));
        Serial.println(incoming);

        // --- 1. GATEWAY NETWORK HANDSHAKE UNLOCK ---
        if (incoming.startsWith("<ACK;")) {
            waitingForEdgeAck = false;
            LoRa.receive();
            return; 
        }
        
        // --- 2. DYNAMIC SPREADING FACTOR ADAPTATION ---
        int sfIdx = incoming.indexOf("SF:");
        if (sfIdx != -1) {
            int endSfIdx = incoming.indexOf(';', sfIdx);
            if (endSfIdx == -1) endSfIdx = incoming.indexOf('>', sfIdx);
            int newSF = incoming.substring(sfIdx + 3, endSfIdx).toInt();
            if (newSF >= 7 && newSF <= 12 && newSF != meterCurrentSF) {
                meterCurrentSF = newSF;
                LoRa.setSpreadingFactor(meterCurrentSF);
                // Save to EEPROM when SF changes
                saveCurrentMeterToEEPROM(false);
            }
        }

        // --- 3. DYNAMIC TELEMETRY TIMING CONSTRAINTS ---
        int intIdx = incoming.indexOf("INT:");
        if (intIdx != -1) {
            int endIntIdx = incoming.indexOf(';', intIdx);
            if (endIntIdx == -1) endIntIdx = incoming.indexOf('>', intIdx);
            unsigned long newInterval = (unsigned long)strtoul(incoming.substring(intIdx + 4, endIntIdx).c_str(), NULL, 10);
            if (newInterval >= 1000 && newInterval <= 3600000 && newInterval != telemetryIntervalMs) {
                telemetryIntervalMs = newInterval;
            }
        }
        
        // --- 4. SECURE PAYLOAD DECODER ENGINE ---
        int txidIdx = incoming.indexOf("TXID:");
        if (txidIdx != -1) {
            int endTxIdx = incoming.indexOf(';', txidIdx);
            if (endTxIdx == -1) endTxIdx = incoming.indexOf('>', txidIdx);
            
            String idString = incoming.substring(txidIdx + 5, endTxIdx);
            idString.trim();
            uint32_t incomingTxId = (uint32_t)strtoul(idString.c_str(), NULL, 10);

            if (incomingTxId == lastProcessedTxId && incomingTxId != 0) {
                if (incoming.indexOf("RECH:") != -1) sendImmediateAck(incomingTxId, "RECH_OK");
                if (incoming.indexOf("TARI:") != -1) sendImmediateAck(incomingTxId, "TARI_OK");
                LoRa.receive(); 
                return;
            }

            // ACTION A: BALANCE CREDIT DEPOSIT ROUTINE
            int rechIdx = incoming.indexOf("RECH:");
            if (rechIdx != -1) {
                int endIdx = incoming.indexOf(';', rechIdx);
                if (endIdx == -1) endIdx = incoming.indexOf('>', rechIdx);
                
                String valStr = incoming.substring(rechIdx + 5, endIdx);
                valStr.trim();
                float rechargeAmount = valStr.toFloat();

                if (rechargeAmount > 0.0f) {
                    accountBalance += rechargeAmount; 
                    lastProcessedTxId = incomingTxId;  
                    saveCurrentMeterToEEPROM(false);   
                    lcd_showCardAccepted(accountBalance); 
                    sendImmediateAck(lastProcessedTxId, "RECH_OK"); 
                }
            }

            // ACTION B: LIVE BASE GRID TARIFF RATE SCALE UPDATER
            int tariIdx = incoming.indexOf("TARI:");
            if (tariIdx != -1) {
                int endIdx = incoming.indexOf(';', tariIdx);
                if (endIdx == -1) endIdx = incoming.indexOf('>', tariIdx);
                
                String tarStr = incoming.substring(tariIdx + 5, endIdx);
                tarStr.trim();
                float newRate = tarStr.toFloat();

                if (newRate > 0.0f) {
                    tariffRate = newRate;             
                    lastProcessedTxId = incomingTxId; 
                    saveCurrentMeterToEEPROM(false);  
                    sendImmediateAck(lastProcessedTxId, "TARI_OK");
                }
            }
        }
    }
    LoRa.receive(); 
}

void sendImmediateAck(uint32_t txid, const char* statusMsg) {
    char ackPayload[64];
    snprintf(ackPayload, sizeof(ackPayload), "<ACK;%s;TXID:%lu;STATUS:%s;SF:%d>", 
             NODE_ID, txid, statusMsg, meterCurrentSF);
    
    LoRa.beginPacket();
    LoRa.print(ackPayload);
    LoRa.endPacket(); 
    
    Serial.print(F("SENT: "));
    Serial.println(ackPayload);
    
    LoRa.receive();
}

void sendLoRaTelemetry() {
    unsigned long currentMillis = millis();

    if (waitingForEdgeAck && (currentMillis - loraTxTimestamp >= 5000)) {
        waitingForEdgeAck = false; 
    }

    if (waitingForEdgeAck) {
        return; 
    }

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

    snprintf(payload, sizeof(payload), "<%s;V:%s;I:%s;P:%s;PF:%s;BAL:%s;RATE:%s;LAST_ID:%lu;DT:%s;SF:%d>",
             NODE_ID, v, i, p, pf, bal, rate, lastProcessedTxId, dt_str, meterCurrentSF);

    LoRa.beginPacket();
    LoRa.print(payload);
    LoRa.endPacket(); 
    
    Serial.print(F("SENT: "));
    Serial.println(payload);
    
    waitingForEdgeAck = true; 
    loraTxTimestamp = currentMillis; 
    
    LoRa.receive();
}