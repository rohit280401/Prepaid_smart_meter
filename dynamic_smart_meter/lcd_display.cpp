#include "lcd_display.h"
#include "config.h"
#include "globals.h"
#include <LiquidCrystal_I2C.h>
#include <Arduino.h>

// Tracks which diagnostic metrics slide to display on the 16x2 grid area
static int screenCycleState = 0;

// ============================================================================
// DISPLAY MATRIX HARDWARE INITIALIZATION
// ============================================================================
void lcd_init() {
    lcd.init();
    lcd.backlight();
    lcd.clear();
    
    // Splash Boot Status Screen
    lcd.setCursor(0, 0);
    lcd.print(F("SMART METER v2.0"));
    lcd.setCursor(0, 1);
    lcd.print(F("OTA LORA ONLINE "));
    delay(2000); 
    lcd.clear();
}

/**
 * @brief Utility function to cleanly pad single-digit metrics with a leading zero.
 */
void printTwoDigits(uint8_t number) {
    if (number < 10) {
        lcd.print('0');
    }
    lcd.print(number);
}

// ============================================================================
// BACKGROUND RUNTIME DASHBOARD GRAPHICS (Cycled Every Second)
// ============================================================================
void lcd_updateParameters() {
    lcd.clear();
    
    switch (screenCycleState) {
        case 0: // --- DASHBOARD 1: FINANCIAL BALANCE & LOAD STATUS ---
            lcd.setCursor(0, 0);
            lcd.print(F("Wallet:"));
            lcd.print(accountBalance, 2 ); // Truncated to 1 decimal place to fit layout perfectly
            
            lcd.setCursor(0, 1);
            lcd.print(F("Status: "));
            if (loadState) {
                lcd.print(F("CONNECTED"));
            } else {
                lcd.print(F("CUT (EMPTY)"));
            }
            break;

        case 1: // --- DASHBOARD 2: MAINS WAVEFORM FRONT-END VOLTAGE & AMPERAGE ---
            lcd.setCursor(0, 0);
            lcd.print(F("Grid V: "));
            lcd.print(Voltage, 1);
            lcd.print(F(" V"));
            
            lcd.setCursor(0, 1);
            lcd.print(F("Load I: "));
            lcd.print(Current, 2);
            lcd.print(F(" A"));
            break;

        case 2: // --- DASHBOARD 3: ACTIVE SYSTEM LOAD POWER ANALYSIS ---
            lcd.setCursor(0, 0);
            lcd.print(F("Power: "));
            lcd.print(Power, 1);
            lcd.print(F(" W"));
            
            lcd.setCursor(0, 1);
            lcd.print(F("P.Factor: "));
            lcd.print(PF, 2);
            break;

        case 3: // --- DASHBOARD 4: LOW-POWER SHADOW SYSTEM TIME DISPLAY ---
            lcd.setCursor(0, 0);
            lcd.print(F("Tariff: "));
            lcd.print(tariffRate, 2);
            lcd.print(F("/kWh"));
            
            lcd.setCursor(0, 1);
            lcd.print(F("Time  : "));
            // Safely prints components directly from low-power RAM variables
            printTwoDigits(rtcHour);
            lcd.print(':');
            printTwoDigits(rtcMinute);
            lcd.print(':');
            printTwoDigits(rtcSecond);
            break;
    }

    // Step to next page layout dashboard index, resetting after index 3
    screenCycleState = (screenCycleState + 1) % 4;
}

// ============================================================================
// HIGH-PRIORITY TRANSACTION CONFIRMATION OVERLAY FLASH
// ============================================================================
void lcd_showCardAccepted(float newBalance) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("TX RECHARGE OK! "));
    lcd.setCursor(0, 1);
    lcd.print(F("New Bal: INR"));
    lcd.print(newBalance, 1);
    
    // Hold block on execution loop briefly so user can see it
    delay(2500);
    lcd.clear();
}