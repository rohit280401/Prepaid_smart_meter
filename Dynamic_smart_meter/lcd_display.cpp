#include "lcd_display.h"
#include "config.h"
#include "globals.h"
#include "lora_ops.h"
#include <LiquidCrystal_I2C.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>

static uint8_t       page = 0;
static bool          overlayOn = false;
static unsigned long overlayEnd = 0;

// 16-column discipline: overflow does not wrap, it vanishes into hidden DDRAM,
// so the operator sees a truncated line and never knows. Every layout is built
// into a 17-byte buffer and padded to exactly 16, which also lets us skip
// lcd.clear() and removes the flicker. snprintf_P keeps formats in flash.
static char l1[LCD_COLS + 1], l2[LCD_COLS + 1];

static void pad(char* s) {
    uint8_t n = strlen(s);
    while (n < LCD_COLS) s[n++] = ' ';
    s[LCD_COLS] = 0;
}

static void draw() {
    pad(l1); pad(l2);
    lcd.setCursor(0, 0); lcd.print(l1);
    lcd.setCursor(0, 1); lcd.print(l2);
}

void lcd_init() {
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(F("PREPAID METER"));
    lcd.setCursor(0, 1); lcd.print(F("Booting..."));
    delay(1000);            // safe here only: runs before the gasp ISR is armed
    wdt_reset();
    lcd.clear();
}

void lcd_update() {
    if (overlayOn) return;
    char a[12], b[12];

    switch (page) {
    case 0:
        dtostrf(mpToInr(balance_mp), 1, 2, a);
        snprintf_P(l1, sizeof(l1), PSTR("Bal %s"), a);
        // Explain a zero reading. Without this the operator sees 0.0 W on the
        // next page and concludes the sensor is broken, when the relay is open
        // by design because there is no credit.
        if (balance_mp <= 0) {
            snprintf_P(l2, sizeof(l2), PSTR("NO CREDIT-CUT"));
        } else if (loraHttpMode()) {
            // The operator needs to know the radio link is down even though
            // data is still reaching the utility.
            snprintf_P(l2, sizeof(l2), PSTR("Load:%s NET"),
                       loadState ? "ON " : "CUT");
        } else {
            snprintf_P(l2, sizeof(l2), PSTR("Load:%s SF%u"),
                       loadState ? "ON " : "CUT", (unsigned)currentSF);
        }
        break;

    case 1:
        dtostrf(Voltage, 1, 1, a); dtostrf(Current, 1, 3, b);
        snprintf_P(l1, sizeof(l1), PSTR("Volt %sV"), a);
        snprintf_P(l2, sizeof(l2), PSTR("Curr %sA"), b);
        break;

    case 2:
        if (balance_mp <= 0) {
            snprintf_P(l1, sizeof(l1), PSTR("Pwr  0.0W"));
            snprintf_P(l2, sizeof(l2), PSTR("(relay open)"));
        } else {
            dtostrf(Power, 1, 1, a); dtostrf(PF, 1, 2, b);
            snprintf_P(l1, sizeof(l1), PSTR("Pwr  %sW"), a);
            snprintf_P(l2, sizeof(l2), PSTR("PF   %s"), b);
        }
        break;

    case 3:
        dtostrf(mpToInr(tariff_mp), 1, 2, a);
        snprintf_P(l1, sizeof(l1), PSTR("Rate %s/kWh"), a);
        // Three states, three messages. "chip missing" and "clock needs setting"
        // have different fixes -- a soldering iron versus a serial command.
        if (!rtcOk)           snprintf_P(l2, sizeof(l2), PSTR("RTC NOT FOUND"));
        else if (!rtcTimeSet) snprintf_P(l2, sizeof(l2), PSTR("CLOCK NOT SET"));
        else snprintf_P(l2, sizeof(l2), PSTR("Time %02u:%02u:%02u"),
                        (unsigned)rtcHour, (unsigned)rtcMinute, (unsigned)rtcSecond);
        break;
    }
    draw();
    page = (page + 1) & 3;
}

// Non-blocking. The original called delay(2500) here, which blocked loop() and
// therefore the gasp handler. Mains failing during a recharge confirmation --
// exactly when the wallet holds the most money -- drained the capacitor before
// the balance reached EEPROM.
void lcd_overlay(const char* a, const char* b) {
    snprintf_P(l1, sizeof(l1), PSTR("%s"), a);
    snprintf_P(l2, sizeof(l2), PSTR("%s"), b);
    draw();
    overlayOn = true;
    overlayEnd = millis() + LCD_OVERLAY_MS;
}

void lcd_rechargeOk(int32_t mp) {
    char amt[12], b[LCD_COLS + 1];
    dtostrf(mpToInr(mp), 1, 2, amt);
    snprintf_P(b, sizeof(b), PSTR("Bal %s"), amt);
    lcd_overlay("RECHARGE OK", b);
}

void lcd_service() {
    if (!overlayOn) return;
    if ((long)(millis() - overlayEnd) >= 0) { overlayOn = false; lcd_update(); }
}