#include <SPI.h>
#include <LoRa.h>

#define LORA_SS    10  
#define LORA_RST    7   
#define LORA_DIO0   2   

// ============================================================================
// CONFIGURATION
// ============================================================================
#define MIN_SF 7
#define MAX_SF 12
#define DEFAULT_SF 7

// RSSI thresholds for SF adaptation
#define RSSI_EXCELLENT -85   // Excellent signal -> SF7
#define RSSI_GOOD      -95   // Good signal -> SF8
#define RSSI_FAIR      -105  // Fair signal -> SF9
#define RSSI_POOR      -115  // Poor signal -> SF10-11
#define RSSI_VERY_POOR -120  // Very poor signal -> SF12

// SNR thresholds for fine-tuning
#define SNR_EXCELLENT 10.0
#define SNR_GOOD      5.0
#define SNR_FAIR      0.0
#define SNR_POOR      -5.0

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================
uint32_t outboundEdgeTxId = 0; 
bool hasSyncedWithMeter = false;
int currentSF = DEFAULT_SF;
int lastSF = DEFAULT_SF;
unsigned long lastSFChangeTime = 0;
const unsigned long SF_COOLDOWN_MS = 30000; // 30 second cooldown

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================
void handleIncomingTelemetry();
void sendAckToMeter(uint32_t txid, const char* statusMsg, int targetSF, int rssi, float snr);
int calculateOptimalSF(int rssi, float snr);
void updateSF(int newSF);

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(9600); 
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
    
    if (!LoRa.begin(865E6)) {
        Serial.println("Lora Init Failed!");
        while(1);
    }
    
    LoRa.setSpreadingFactor(currentSF);
    LoRa.receive();
    
    Serial.println(F("========================================"));
    Serial.println(F("UTILITY CENTER - Auto SF Adaptation"));
    Serial.println(F("SF Range: 7-12 (Fully Automatic)"));
    Serial.println(F("========================================"));
    Serial.println(F("Commands:"));
    Serial.println(F("  RECH:<amount>  - Recharge meter"));
    Serial.println(F("  TARI:<rate>    - Set tariff rate"));
    Serial.println(F("  STATUS         - Show current status"));
    Serial.println(F("========================================"));
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        handleIncomingTelemetry();
    }

    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        
        // STATUS command (only manual command left)
        if (input.equalsIgnoreCase("STATUS")) {
            Serial.println(F("\n=== UTILITY CENTER STATUS ==="));
            Serial.print(F("Current SF: ")); Serial.println(currentSF);
            Serial.print(F("Synced with meter: ")); Serial.println(hasSyncedWithMeter ? "YES" : "NO");
            Serial.print(F("Last TX ID: ")); Serial.println(outboundEdgeTxId);
            Serial.println(F("=============================\n"));
            return;  // FIXED: Use return instead of continue
        }
        
        if (!hasSyncedWithMeter) {
            Serial.println(F("ERROR: Not synced with meter yet. Waiting for first telemetry packet..."));
            return;
        }

        // Action 1: Recharge Downlink
        if (input.startsWith("RECH:")) {
            outboundEdgeTxId++;
            String val = input.substring(5);
            val.trim();
            String cmd = "<Node1;TXID:" + String(outboundEdgeTxId) + ";RECH:" + val + ";SF:" + String(currentSF) + ">";
            
            LoRa.setSpreadingFactor(currentSF); 
            LoRa.beginPacket();
            LoRa.print(cmd);
            LoRa.endPacket();
            
            Serial.print(F("SENT: ")); Serial.println(cmd);
            LoRa.receive();
        }
        
        // Action 2: Tariff Downlink
        else if (input.startsWith("TARI:")) {
            outboundEdgeTxId++;
            String val = input.substring(5);
            val.trim();
            String cmd = "<Node1;TXID:" + String(outboundEdgeTxId) + ";TARI:" + val + ";SF:" + String(currentSF) + ">";
            
            LoRa.setSpreadingFactor(currentSF);
            LoRa.beginPacket();
            LoRa.print(cmd);
            LoRa.endPacket();
            
            Serial.print(F("SENT: ")); Serial.println(cmd);
            LoRa.receive();
        }
        
        // Unknown command
        else {
            Serial.println(F("Unknown command. Available: RECH:<val>, TARI:<val>, STATUS"));
        }
    }
}

// ============================================================================
// CALCULATE OPTIMAL SPREADING FACTOR BASED ON RSSI AND SNR
// ============================================================================
int calculateOptimalSF(int rssi, float snr) {
    int recommendedSF = DEFAULT_SF;
    
    // Step 1: Base decision on RSSI
    if (rssi >= RSSI_EXCELLENT) {
        // Excellent signal - use SF7 for max speed
        recommendedSF = 7;
    } 
    else if (rssi >= RSSI_GOOD) {
        // Good signal - use SF8
        recommendedSF = 8;
    } 
    else if (rssi >= RSSI_FAIR) {
        // Fair signal - use SF9
        recommendedSF = 9;
    } 
    else if (rssi >= RSSI_POOR) {
        // Poor signal - use SF10
        recommendedSF = 10;
    } 
    else if (rssi >= RSSI_VERY_POOR) {
        // Very poor signal - use SF11
        recommendedSF = 11;
    } 
    else {
        // Extremely poor signal - use SF12 for max range
        recommendedSF = 12;
    }
    
    // Step 2: Fine-tune based on SNR
    if (snr >= SNR_EXCELLENT && recommendedSF > 7) {
        // Excellent SNR can use lower SF
        recommendedSF--;
    } 
    else if (snr >= SNR_GOOD && recommendedSF > 8) {
        // Good SNR can drop by 1 if we're at high SF
        if (recommendedSF > 9) recommendedSF--;
    } 
    else if (snr <= SNR_POOR && recommendedSF < 12) {
        // Poor SNR needs higher SF
        recommendedSF++;
    }
    
    // Step 3: Clamp to valid range
    if (recommendedSF < MIN_SF) recommendedSF = MIN_SF;
    if (recommendedSF > MAX_SF) recommendedSF = MAX_SF;
    
    return recommendedSF;
}

// ============================================================================
// UPDATE SF WITH COOLDOWN
// ============================================================================
void updateSF(int newSF) {
    if (newSF == currentSF) return;
    
    unsigned long now = millis();
    if (now - lastSFChangeTime < SF_COOLDOWN_MS) {
        // Don't spam with cooldown messages
        return;
    }
    
    lastSF = currentSF;
    currentSF = newSF;
    lastSFChangeTime = now;
    
    LoRa.setSpreadingFactor(currentSF);
    
    Serial.print(F("SF AUTO-ADAPTED: "));
    Serial.print(lastSF);
    Serial.print(F(" -> "));
    Serial.println(currentSF);
}

// ============================================================================
// HANDLE INCOMING TELEMETRY
// ============================================================================
void handleIncomingTelemetry() {
    String incoming = "";
    while (LoRa.available()) {
        incoming += (char)LoRa.read();
    }
    incoming.trim(); 

    int rssi = LoRa.packetRssi();
    float snr = LoRa.packetSnr();

    if (incoming.startsWith("<") && incoming.endsWith(">") && incoming.indexOf("Node1") != -1) {
        if (incoming.startsWith("<ACK;")) return;

        int idIdx = incoming.indexOf("LAST_ID:");
        if (idIdx != -1) {
            int endIdx = incoming.indexOf(';', idIdx);
            if (endIdx == -1) endIdx = incoming.indexOf('>', idIdx);
            uint32_t meterTxId = (uint32_t)strtoul(incoming.substring(idIdx + 8, endIdx).c_str(), NULL, 10);
            
            if (!hasSyncedWithMeter || meterTxId > outboundEdgeTxId) {
                outboundEdgeTxId = meterTxId;
                hasSyncedWithMeter = true;
                Serial.println(F("\n=== METER SYNCED ==="));
                Serial.print(F("TX ID: ")); Serial.println(outboundEdgeTxId);
                Serial.println(F("====================\n"));
            }
            
            // Print received telemetry with signal info
            Serial.print(F("RECEIVED: ")); Serial.println(incoming);
            Serial.print(F("  RSSI: ")); Serial.print(rssi);
            Serial.print(F(" dBm, SNR: ")); Serial.print(snr, 1);
            Serial.println(F(" dB"));

            // AUTOMATIC SF ADAPTATION - NO MANUAL CONTROL
            int optimalSF = calculateOptimalSF(rssi, snr);
            
            if (optimalSF != currentSF) {
                updateSF(optimalSF);
            }

            // Send ACK with current SF and signal metrics
            sendAckToMeter(meterTxId, "TELEMETRY_OK", currentSF, rssi, snr);
        }
    }
}

// ============================================================================
// SEND ACKNOWLEDGMENT TO METER
// ============================================================================
void sendAckToMeter(uint32_t txid, const char* statusMsg, int targetSF, int rssi, float snr) {
    String ackPayload = "<ACK;Node1;TXID:" + String(txid) + 
                        ";STATUS:" + String(statusMsg) + 
                        ";SF:" + String(targetSF) + 
                        ";RSSI:" + String(rssi) + 
                        ";SNR:" + String(snr, 1) + ">";
    
    LoRa.setSpreadingFactor(targetSF);
    LoRa.beginPacket();
    LoRa.print(ackPayload);
    LoRa.endPacket();
    
    Serial.print(F("SENT: ")); Serial.println(ackPayload);
    LoRa.receive(); 
}