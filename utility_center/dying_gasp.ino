#include <Wire.h>
#include <RTClib.h>
#include <SPI.h>
#include <LoRa.h>
#include <EEPROM.h>

// ---------------- LORA PINS ----------------
#define SS_PIN    10
#define RST_PIN   9
#define DIO0_PIN  2

// ---------------- POWER FAIL SENSE ----------------
#define POWER_SENSE_PIN A3

RTC_DS3231 rtc;

// Simulated meter values
float energyBalance = 0;
float totalUnitsConsumed = 0;

// Prevent multiple dying-gasp executions
bool powerFailHandled = false;

// Simulated update interval
unsigned long lastUpdate = 0;
const unsigned long interval = 5000;

// EEPROM structure
struct PowerLog
{
  float balance;
  float consumed;
  uint16_t marker;
};

// =================================================
// SETUP
// =================================================
void setup()
{
  Serial.begin(9600);
  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("DYING GASP SMART METER TEST");
  Serial.println("================================");

  // ---------------- RTC ----------------
  Wire.begin();

  if (!rtc.begin())
  {
    Serial.println("RTC NOT FOUND");
    while (1);
  }

  Serial.println("RTC OK");

  // ---------------- LORA ----------------
  LoRa.setPins(SS_PIN, RST_PIN, DIO0_PIN);

  if (!LoRa.begin(865E6))
  {
    Serial.println("LORA INIT FAILED");
    while (1);
  }

  Serial.println("LORA OK");

  // ---------------- EEPROM RECOVERY ----------------
  PowerLog savedData;

  EEPROM.get(0, savedData);

  if (savedData.marker == 0x55AA)
  {
    Serial.println();
    Serial.println("EEPROM RECOVERY DATA FOUND");

    Serial.print("Recovered Balance : ");
    Serial.println(savedData.balance);

    Serial.print("Recovered Units   : ");
    Serial.println(savedData.consumed);

    // Clear marker
    savedData.marker = 0;
    EEPROM.put(0, savedData);

    Serial.println("EEPROM FLAG CLEARED");
  }
  else
  {
    Serial.println("No Stored Dying Gasp Data");
  }

  randomSeed(analogRead(A0));
}

// =================================================
// LOOP
// =================================================
void loop()
{
  // ------------------------------------------------
  // Monitor Power Sense Line
  // ------------------------------------------------

  int powerLevel = analogRead(POWER_SENSE_PIN);

  // Your measurements:
  // Normal ≈ 617
  // Falling starts around 570
  // Trigger early to preserve capacitor energy

  if (!powerFailHandled && powerLevel < 560)
  {
    runDyingGasp();
  }

  // ------------------------------------------------
  // Simulated Meter Updates
  // ------------------------------------------------

  if (millis() - lastUpdate >= interval)
  {
    lastUpdate = millis();

    energyBalance      = random(500, 100000) / 100.0;
    totalUnitsConsumed = random(10, 1000) / 100.0;

    Serial.print("Balance = ");
    Serial.print(energyBalance);

    Serial.print(" | Units = ");
    Serial.println(totalUnitsConsumed);

    Serial.print("A3 = ");
    Serial.println(powerLevel);

    Serial.println();
  }
}

// =================================================
// DYING GASP HANDLER
// =================================================
void runDyingGasp()
{
  if (powerFailHandled)
    return;

  powerFailHandled = true;

  unsigned long startTime = micros();

  Serial.println();
  Serial.println("POWER FAILURE DETECTED");

  // Read RTC
  DateTime now = rtc.now();

  // ------------------------------------------------
  // STEP 1 : SAVE DATA TO EEPROM FIRST
  // ------------------------------------------------

  PowerLog backup;

  backup.balance  = energyBalance;
  backup.consumed = totalUnitsConsumed;
  backup.marker   = 0x55AA;

  EEPROM.put(0, backup);

  Serial.println("EEPROM WRITE COMPLETE");

  Serial.print("Saved Balance = ");
  Serial.println(backup.balance, 2);

  Serial.print("Saved Units   = ");
  Serial.println(backup.consumed, 2);

  // ------------------------------------------------
  // STEP 2 : BUILD LORA PACKET
  // ------------------------------------------------

  char balanceStr[15];
  char unitsStr[15];

  dtostrf(energyBalance, 0, 2, balanceStr);
  dtostrf(totalUnitsConsumed, 0, 2, unitsStr);

  char packet[120];

  sprintf(packet,
          "POWER_GONE,%04d-%02d-%02d,%02d:%02d:%02d,%s,%s",
          now.year(),
          now.month(),
          now.day(),
          now.hour(),
          now.minute(),
          now.second(),
          balanceStr,
          unitsStr);

  // ------------------------------------------------
  // STEP 3 : SEND LORA MESSAGE
  // ------------------------------------------------

  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();

  Serial.print("LoRa Sent: ");
  Serial.println(packet);

  // ------------------------------------------------
  // STEP 4 : TIMING
  // ------------------------------------------------

  unsigned long elapsedTime = micros() - startTime;

  Serial.print("Execution Time = ");
  Serial.print(elapsedTime / 1000.0);
  Serial.println(" ms");

  while (1);
}