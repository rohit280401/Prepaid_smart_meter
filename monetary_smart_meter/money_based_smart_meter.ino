#include <SPI.h>
#include <EmonLib.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <LoRa.h>
#include <RTClib.h>
#include <Arduino.h>

#include "config.h"
#include "globals.h"
#include "lora_ops.h"
#include "lcd_display.h"
#include "power_measure.h"
#include "powerManagement.h"

// Instantiate underlying standard processing structures
EnergyMonitor emon;
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
RTC_DS3231 rtc;

// Define default global operational constraints
volatile float accountBalance = 0.0;
volatile float tariffRate     = 6.50; // Dynamic utility initialization rate (₹6.50 per kWh)
volatile uint32_t lastProcessedTxId = 0;

volatile float Voltage = 0.0;
volatile float Current = 0.0;
volatile float Power   = 0.0;
volatile float PF      = 0.0;

// Instantiate the global low-power RAM shadow time variables
volatile uint16_t rtcYear   = 2026;
volatile uint8_t  rtcMonth  = 1;
volatile uint8_t  rtcDay    = 1;
volatile uint8_t  rtcHour   = 0;
volatile uint8_t  rtcMinute = 0;
volatile uint8_t  rtcSecond = 0;

volatile bool loadState          = false;
volatile bool powerFailHandled   = false;
volatile bool loraPacketReceived = false; 
volatile unsigned long lastTime  = 0;

// Tracking registers for non-blocking software scheduling
unsigned long lastPowerTime = 0;
unsigned long lastLcdTime   = 0;
unsigned long lastLoraTime  = 0;

// ============================================================================
// LORA RX-DONE HARDWARE INTERRUPT SERVICE ROUTINE (ISR)
// ============================================================================
void loraInterruptISR() {
    loraPacketReceived = true;
}

void setup() {
    Serial.begin(9600);
    SPI.begin();
    Wire.begin();
    Wire.setWireTimeout(25000, true); // Prevents I2C bus lockups if an LCD cable disconnects

    // --- INITIALIZE REAL TIME CLOCK FIRST ---
    if (!rtc.begin()) {
        Serial.println(F("RTC Error."));
        while (1);
    }

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH); // Start disconnected for consumer load isolation safety
    lcd_init();

    // --- 1. Map Dying Gasp Blackout Protection Pin (Pin 3 / Interrupt 1) ---
    pinMode(POWER_SENSE_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(POWER_SENSE_PIN), runDyingGaspISR, FALLING);

    // --- 2. Recover State History from Internal EEPROM Storage Registers ---
    bool powerOffDetected = false;
    recoverSavedMeterData(powerOffDetected);

    // --- 3. Initialize LoRa Engine and Event Interrupts (Pin 2 / Interrupt 0) ---
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
    if (!LoRa.begin(LORA_FREQ)) {
        Serial.println(F("LoRa Error."));
        while (1);
    }
    
    pinMode(LORA_DIO0, INPUT);
    attachInterrupt(digitalPinToInterrupt(LORA_DIO0), loraInterruptISR, RISING);
    
    // Explicitly command the LoRa transceiver to enter continuous listening mode
    LoRa.receive(); 

    // Setup energy monitoring configuration fields
    emon.voltage(VOLT_PIN, VOLTAGE_CAL, VOLTAGE_PHASE);
    emon.current(CURR_PIN, CURRENT_CAL);
    lastTime = millis();
    Serial.println(F("Wireless Dynamic Prepaid Meter Online. Initialized via Asynchronous Interrupts."));
}

void loop() {
    unsigned long currentTime = millis();

    // ========================================================================
    // ASYNCHRONOUS EVENT-DRIVEN PACKET PARSING (No Polling Overheads)
    // ========================================================================
    if (loraPacketReceived) {
        loraPacketReceived = false; 
        handleIncomingLoRaPacket(); 
    }

    // ========================================================================
    // SCHEDULER: POWER MEASUREMENT ENGINE & RE-BILLING INTERFACE
    // ========================================================================
    if (currentTime - lastPowerTime >= POWER_INTERVAL_MS) {
        processPower();
        
        // --- LOW POWER OPTIMIZATION: REFRESH SHADOW REGISTERS IN RAM ---
        // We poll the physical RTC once per second here so the ISR can avoid I2C calls
        DateTime now = rtc.now();
        rtcYear   = now.year();
        rtcMonth  = now.month();
        rtcDay    = now.day();
        rtcHour   = now.hour();
        rtcMinute = now.minute();
        rtcSecond = now.second();

        serialOutput();
        lastPowerTime = currentTime;
    }

    // ========================================================================
    // SCHEDULER: PERSISTENT LAYOUT DISPLAY TRANSITIONS
    // ========================================================================
    if (currentTime - lastLcdTime >= LCD_INTERVAL_MS) {
        lcd_updateParameters();
        lastLcdTime = currentTime;
    }

    // ========================================================================
    // SCHEDULER: TELEMETRY AND UPSTREAM DATA SYNC PACKETS
    // ========================================================================
    if (currentTime - lastLoraTime >= LORA_INTERVAL_MS) {
        sendLoRaTelemetry();
        lastLoraTime = currentTime;
    }
}