#include "power_measure.h"
#include "config.h"
#include "globals.h"
#include <EmonLib.h>

extern EnergyMonitor emon;  // defined in main .ino

void processPower() {
  emon.calcVI(20, 2000);

  Voltage = emon.Vrms;
  Current = emon.Irms;
  Power = emon.realPower;
  PF = emon.powerFactor;

  if (Current < 0.1) {
    Current = 0;
    PF = 0;
    loadState = false;
    Power = 0;
  }


  unsigned long now = millis();
  double dt = (now - lastTime) / 1000.0;
  lastTime = now;

  if (energyBalance > 0) {
    loadState = true;
    double consumed = Power * dt / 3600.0;
    energyBalance -= consumed;
    if (energyBalance <= 0) {
      energyBalance = 0;
      loadState = false;
      digitalWrite(RELAY_PIN, HIGH);
      Serial.println("Units finished");
    }
  }
}

void serialOutput() {
  Serial.print("V="); Serial.print(Voltage);
  Serial.print(" I="); Serial.print(Current);
  Serial.print(" P="); Serial.print(Power);
  Serial.print(" PF="); Serial.print(PF);
  Serial.print(" Bal="); Serial.print(energyBalance);
  Serial.print(" Load=");
  Serial.println(loadState ? "ON" : "OFF");
}