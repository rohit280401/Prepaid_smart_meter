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

EnergyMonitor emon;
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
RTC_DS3231 rtc;

// ============================================================================
// GLOBAL VARIABLE DEFINITIONS
// ============================================================================
volatile float accountBalance = 0.0;
volatile float tariffRate = 8.0;
volatile uint32_t lastProcessedTxId = 0;

// These are defined in lora_ops.cpp, but need to be accessible here
extern uint8_t meterCurrentSF;
extern unsigned long telemetryIntervalMs;

volatile float Voltage = 0.0;
volatile float Current = 0.0;
volatile float Power = 0.0;
volatile float PF = 0.0;

volatile uint8_t rtcHour = 0;
volatile uint8_t rtcMinute = 0;
volatile uint8_t rtcSecond = 0;

volatile bool loadState = false;
//volatile bool powerFailHandled = false;
volatile bool loraPacketReceived = false;
volatile unsigned long lastTime = 0;

// ============================================================================
// TIMING VARIABLES
// ============================================================================
unsigned long lastPowerTime = 0;
unsigned long lastLcdTime = 0;
unsigned long lastLoraTime = 0;
// These are defined in powerManagement.cpp
extern volatile bool powerFailHandled;
extern volatile bool dyingGaspPending;
// ============================================================================
// INTERRUPT SERVICE ROUTINES
// ============================================================================
void loraInterruptISR() {
    loraPacketReceived = true;
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(9600);
    SPI.begin();
    Wire.begin();
    Wire.setWireTimeout(25000, true);

    if (!rtc.begin()) {
        Serial.println(F("RTC Error."));
        while (1);
    }

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH);
    
    lcd_init();

    pinMode(POWER_SENSE_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(POWER_SENSE_PIN), runDyingGaspISR, FALLING);

    bool powerOffDetected = false;
    recoverSavedMeterData(powerOffDetected);

    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
    if (!LoRa.begin(LORA_FREQ)) {
        Serial.println(F("LoRa Error."));
        while (1);
    }
    
    pinMode(LORA_DIO0, INPUT);
    attachInterrupt(digitalPinToInterrupt(LORA_DIO0), loraInterruptISR, RISING);
    
    LoRa.receive();
    
    emon.voltage(VOLT_PIN, VOLTAGE_CAL, VOLTAGE_PHASE);
    emon.current(CURR_PIN, CURRENT_CAL);
    lastTime = millis();
    
    Serial.println(F("Wireless Dynamic Prepaid Meter Online."));
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
    unsigned long currentTime = millis();
    
    handleIncomingLoRaPacket();

    if (currentTime - lastPowerTime >= POWER_INTERVAL_MS) {
        processPower();
        
        DateTime now = rtc.now();
        rtcHour = now.hour();
        rtcMinute = now.minute();
        rtcSecond = now.second();

        lastPowerTime = currentTime;
    }

    if (currentTime - lastLcdTime >= LCD_INTERVAL_MS) {
        lcd_updateParameters();
        lastLcdTime = currentTime;
    }

    if (currentTime - lastLoraTime >= telemetryIntervalMs) {
        sendLoRaTelemetry();
        lastLoraTime = currentTime;
    }
}