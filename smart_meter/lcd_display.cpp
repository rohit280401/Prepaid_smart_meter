#include "lcd_display.h"
#include "config.h"
#include "globals.h"
#include <LiquidCrystal_I2C.h>
static uint8_t screen = 0;
static unsigned long lastScreenChange = 0;

extern LiquidCrystal_I2C lcd;   // defined in main .ino

void lcd_init() {
  lcd.init();
  lcd.backlight();
  lcd_showTapCard();
}

void lcd_showTapCard() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("   Tap Card   ");
  lcd.setCursor(0, 1);
  lcd.print("              ");
}

void lcd_updateParameters() {
  if (!cardLoaded) {
    // Let rfid_ops control errors. Only default back if empty.
    return;
  }

  // Cycler rules (runs every 2.5 seconds for visibility comfort)
  if (energyBalance <= 0) {
    lcd.setCursor(0, 0);
    lcd.print("RECHARGE CARD!  "); // Line 1
    lcd.setCursor(0, 1);
    lcd.print("UID: C3BFDD34   ");   // Line 2
    return; // Block the standard parameter cycler below
  }

  if (millis() - lastScreenChange > 2500) {
    screen++;
    if (screen > 3) screen = 0; // Fixed index constraint matching cases 0-3
    lcd.clear();
    lastScreenChange = millis();
  }

  switch(screen) {
    case 0:
      lcd.setCursor(0,0);
      lcd.print("Card UID:");
      lcd.setCursor(0,1);
      lcd.print("C3BFDD34");
      break;

    case 1:
      lcd.setCursor(0,0);
      lcd.print("Bal: ");
      lcd.print(energyBalance, 1);
      lcd.setCursor(0,1);
      lcd.print("Load: ");
      lcd.print(loadState ? "ON " : "OFF");
      break;

    case 2:
      lcd.setCursor(0,0);
      lcd.print("V: ");
      lcd.print(Voltage, 1);
      lcd.setCursor(0,1);
      lcd.print("I: ");
      lcd.print(Current, 2);
      break;

    case 3:
      lcd.setCursor(0,0);
      lcd.print("P: ");
      lcd.print(Power, 1);
      lcd.setCursor(0,1);
      lcd.print("PF: ");
      lcd.print(PF, 2);
      break;
  }
}