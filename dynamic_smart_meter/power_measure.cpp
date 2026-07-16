#include "power_measure.h"
#include "config.h"
#include "globals.h"
#include <EmonLib.h>
#include <math.h> // For fabs()

void processPower() {
  emon.calcVI(20, 2000);

  Voltage = emon.Vrms;
  Current = emon.Irms;
  
  // FIX 1: Use absolute value fabs() to ensure backward CT clamps don't stop billing updates
  Power   = fabs(emon.realPower);
  PF      = fabs(emon.powerFactor);

  if (Current < MIN_CURRENT_THRESHOLD) {
    Current = 0;
    PF      = 0;
    Power   = 0;
  }

  // --- Energy deduction ---
  unsigned long now      = millis();
  unsigned long dtMillis = now - lastTime;
  lastTime = now;

  // Calculate raw Kilowatt-Hours consumed during this window
  float consumedKwh = (Power * (float)dtMillis) / 3600000.0f / 1000.0f; 

  // FIX 2: Multiply energy by your currency tariff rate to calculate the cost in INR Rupees
  float monetaryCost = consumedKwh * tariffRate;

  // FIX 3: Update accountBalance uniformly instead of availableUnits
  if (accountBalance > 0.0f) {
    accountBalance -= monetaryCost;
    if (accountBalance < 0.0f) accountBalance = 0.0f;
  }

  // --- Safe Dynamic Hardware Load Control ---
  if (accountBalance <= 0.0f) {
    loadState = false;
    digitalWrite(RELAY_PIN, HIGH); // Disconnect consumer grid lines safely
    Current = 0;
    PF      = 0;
    Power   = 0;
  } else {
    loadState = true;
    digitalWrite(RELAY_PIN, LOW);  // Maintain connection path
  }
}
/*
void serialOutput() {
  Serial.print(F("V: "));     Serial.print(Voltage, 1);
  Serial.print(F("V | I: ")); Serial.print(Current, 2);
  Serial.print(F("A | P: ")); Serial.print(Power, 1);
  Serial.print(F("W | PF: ")); Serial.print(PF, 2);
  Serial.print(F(" | Wallet Balance: INR ")); Serial.print(accountBalance, 2);
  Serial.println(F(" Rupees"));
}
*/