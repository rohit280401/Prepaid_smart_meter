/*
 * ============================================================================
 *  WIRELESS PREPAID ENERGY METER  --  METER NODE  v5
 * ============================================================================
 *  Sensors: ZMPT101B (voltage), ACS712 (current), DS3231 (RTC), SX1276 (LoRa)
 *  Board:   Arduino Uno / Nano (ATmega328P)
 *  Libs:    arduino-LoRa (sandeepmistry), EmonLib, LiquidCrystal_I2C
 *           (RTClib is NO LONGER required -- see rtc_lite.cpp)
 *
 *  v5 CHANGES -- ALL OF THESE EXIST TO FIT IN 32 KB OF FLASH
 *  --------------------------------------------------------
 *   A. String() eliminated from esp01_ops.cpp. Nine String temporaries were
 *      constructed purely to call .length(); that dragged in the entire String
 *      class (~1.6-2.4 KB). The JSON body is now built once with snprintf_P
 *      and measured. This also FIXES a latent bug: the Content-Length header
 *      was derived from a hand-counted constant (105) which, if wrong by one
 *      byte, hangs the server waiting for data that never arrives.
 *
 *   B. sscanf() removed from SETTIME. It pulled vfscanf, ~1.4 KB, to parse six
 *      integers. strtol does it in twelve lines; strtoul was already linked.
 *
 *   C. RTClib removed. It provided DateTime arithmetic, TimeSpan and string
 *      formatting for a feature that reads six BCD registers and prints them
 *      on one LCD page. rtc_lite.cpp does it directly, and gets a better
 *      "clock not set" signal for free (the DS3231 oscillator-stop flag,
 *      instead of guessing from an implausible year).
 *
 *   D. Build modes (config.h, ESP_ON_HW_SERIAL). In deployment the ESP-01 uses
 *      the hardware UART, and the serial console plus every debug string
 *      compile out. SoftwareSerial is not linked at all.
 *      >>> 115200 baud on SoftwareSerial DOES NOT WORK on a 16 MHz 328P. <
 *      The old code set it and then never parsed a response, so the failure
 *      was invisible -- it looked like it worked.
 *
 *   E. TARIFF_MAX_MP was equal to TARIFF_MIN_MP (both 10000) while the default
 *      was 800000. Every TARI command except exactly INR 0.10 was silently
 *      rejected, and loadMeter()'s clamp wrote a value that failed its own
 *      bounds check on the next boot.
 *
 *   F. Wi-Fi fallback threshold is now SF > 8 (config.h, SF_HTTP_THRESHOLD),
 *      not SF > 9.
 *
 *  THE NINE ORIGINAL BUGS THIS STILL FIXES
 *  ---------------------------------------
 *   1. BOOT HANG on `while (!LoRa.begin(...))` with no exit and no watchdog.
 *      Now bounded; the meter runs offline if the radio never answers.
 *   2. INIT ORDER: setSpreadingFactor() before setPins()/begin().
 *   3. SF DEADLOCK: gateway switched SF then announced it.
 *   4. MONEY WAS A float: at INR 5000, a 3 s window at 10 W deducted exactly
 *      INR 0.00. Now int32 milli-paisa with an exact remainder accumulator.
 *   5. EMPTY WALLET READ AS CORRUPT: power-cycling granted INR 10, unlimited.
 *   6. REPLAY: `id == lastTxId` only rejected the most recent packet.
 *   7. delay(2500) in the recharge screen blocked the blackout handler.
 *   8. DYING GASP ORDER: transmitted (~20 mC) before EEPROM write (~2.2 mC).
 *   9. NO WATCHDOG.
 *
 *  KNOWN LIMITATIONS (state these in your report)
 *  ----------------------------------------------
 *   * Packets are NOT authenticated. The monotonic transaction id blocks replay
 *     of a captured packet, but anyone with a LoRa module on the same frequency
 *     and sync word can forge <Node1;CMD;ID:99999;RECH:50000>.
 *   * The HTTP fallback is fire-and-forget: no AT response is parsed, so a
 *     dropped Wi-Fi link looks identical to a successful POST from the meter's
 *     side. Acceptable only because billing is local and does not depend on it.
 *   * An ACS712-30A resolves ~0.074 A per ADC step; the practical noise floor
 *     is ~0.4 A, so loads under ~92 W bill as ZERO. An SCT-013-030 current
 *     transformer resolves ~0.01 A and is the correct sensor for metering.
 *   * EEPROM is rated 100k writes. At one checkpoint per 5 minutes, ~1.9 years.
 *
 *  HARDWARE REQUIRED
 *  -----------------
 *   * 3.3 V regulator (AMS1117) for the LoRa module, 100 uF + 0.1 uF at its VCC.
 *     The Uno's onboard 3.3 V pin supplies ~50 mA; an SX1276 draws up to 120 mA
 *     transmitting. That sag is the usual cause of bug 1.
 *   * RC filter on the mains sense pin:
 *       1N4148 -> 100k -> node;  node -> 10uF to GND;  node -> 100k to GND
 *   * 0.1 uF from A0 to GND and A1 to GND, close to the Arduino.
 *   * ESP-01 runs at 3.3 V. Its RX needs a divider from the Arduino's 5 V TX,
 *     and it needs its own 3.3 V supply capable of 300 mA peak.
 *
 *  >>> UPLOADING WITH ESP_ON_HW_SERIAL = 1 <
 *  Disconnect the ESP-01 TX line from D0 before flashing, or the bootloader
 *  and the ESP will fight over the UART and the upload will fail.
 * ============================================================================
 */

#include <SPI.h>
#include <Wire.h>
#include <EmonLib.h>
#include <LiquidCrystal_I2C.h>
#include <LoRa.h>
#include <avr/wdt.h>
#include <stdlib.h>

#include "config.h"
#include "globals.h"
#include "rtc_lite.h"
#include "lora_ops.h"
#include "esp01_ops.h"
#include "lcd_display.h"
#include "power_measure.h"
#include "powerManagement.h"

EnergyMonitor     emon;
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);

int32_t  balance_mp = 0;
int32_t  tariff_mp  = TARIFF_DEFAULT_MP;
uint32_t lastTxId   = 0;

uint8_t currentSF = SF_RESCUE;
uint8_t ackMisses = 0;

float Voltage = 0, Current = 0, Power = 0, PF = 0;

uint16_t rtcYear = 2026;
uint8_t  rtcMonth = 1, rtcDay = 1, rtcHour = 0, rtcMinute = 0, rtcSecond = 0;
bool     rtcOk = false, rtcTimeSet = false;

bool          loadState  = false;
unsigned long lastBillMs = 0;

static unsigned long tPower = 0, tLcd = 0, tLora = 0;

// ===========================================================================
//  SERIAL CONSOLE -- bench builds only (ESP_ON_HW_SERIAL 0)
// ===========================================================================
#if ENABLE_CONSOLE
static char cbuf[48];
static uint8_t clen = 0;

// Replaces sscanf("%d-%d-%d %d:%d:%d"). vfscanf costs ~1.4 KB of flash to
// parse six integers; this costs about forty bytes.
static long nextNum(char** p) {
    while (**p && (**p < '0' || **p > '9')) (*p)++;
    if (!**p) return -1;
    return strtol(*p, p, 10);
}

static void doCommand(char* s) {
    int n = strlen(s);
    while (n > 0 && (s[n-1] == '\r' || s[n-1] == ' ')) s[--n] = 0;
    if (!n) return;

    if (!strcasecmp(s, "STATUS")) {
        Serial.print(F("bal="));    Serial.print(mpToInr(balance_mp), 5);
        Serial.print(F(" rate="));  Serial.print(mpToInr(tariff_mp), 2);
        Serial.print(F(" relay=")); Serial.print(loadState ? F("ON") : F("CUT"));
        Serial.print(F(" V="));     Serial.print(Voltage, 1);
        Serial.print(F(" I="));     Serial.print(Current, 3);
        Serial.print(F(" P="));     Serial.print(Power, 1);
        Serial.print(F(" PF="));    Serial.print(PF, 2);
        Serial.print(F(" SF="));    Serial.print(currentSF);
        Serial.print(F(" txid="));  Serial.print(lastTxId);
        Serial.print(F(" RTC="));
        if (!rtcOk)           Serial.println(F("MISSING"));
        else if (!rtcTimeSet) Serial.println(F("NOT SET"));
        else                  Serial.println(F("ok"));
        return;
    }

    if (!strcasecmp(s, "CAL")) {
        for (uint8_t i = 0; i < 10; i++) {
            wdt_reset();
            emon.calcVI(20, 2000);
            Serial.print(F("Vrms ")); Serial.print(emon.Vrms, 1);
            Serial.print(F("  Irms ")); Serial.println(emon.Irms, 4);
        }
        return;
    }

    if (!strncasecmp(s, "TESTRECH:", 9)) {
        float amt = atof(s + 9);
        if (amt <= 0 || amt > 10000) { Serial.println(F("[ERR] 0.01..10000")); return; }
        int64_t v = (int64_t)balance_mp + inrToMp(amt);
        if (v > BALANCE_MAX_MP) v = BALANCE_MAX_MP;
        balance_mp = (int32_t)v;
        saveMeter(false);
        lcd_rechargeOk(balance_mp);
        return;
    }

    if (!strncasecmp(s, "SETTIME:", 8)) {
        char* q = s + 8;
        long Y  = nextNum(&q), Mo = nextNum(&q), D  = nextNum(&q);
        long H  = nextNum(&q), Mi = nextNum(&q), S  = nextNum(&q);
        if (Y < 2024 || Y > 2099 || Mo < 1 || Mo > 12 || D < 1 || D > 31 ||
            H < 0 || H > 23 || Mi < 0 || Mi > 59 || S < 0 || S > 59) {
            Serial.println(F("[ERR] SETTIME:2026-07-31 14:30:00"));
            return;
        }
        if (rtcLiteSet((uint16_t)Y, (uint8_t)Mo, (uint8_t)D,
                       (uint8_t)H, (uint8_t)Mi, (uint8_t)S))
            Serial.println(F("[RTC] set"));
        else
            Serial.println(F("[RTC] I2C fail"));
        return;
    }

    Serial.println(F("[ERR] STATUS|CAL|TESTRECH:<amt>|SETTIME:<...>"));
}

static void console() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') { cbuf[clen] = 0; doCommand(cbuf); clen = 0; }
        else if (clen < sizeof(cbuf) - 1) cbuf[clen++] = c;
    }
}
#endif  // ENABLE_CONSOLE

// ===========================================================================
void setup() {
    wdt_disable();
    Serial.begin(SERIAL_BAUD);
    delay(50);
    DBGLN(F("\n=== PREPAID METER v5 ==="));

    digitalWrite(RELAY_PIN, RELAY_OPEN);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, RELAY_OPEN);

    pinMode(POWER_SENSE_PIN, INPUT_PULLUP);

    Wire.begin();
    Wire.setWireTimeout(25000, true);

    pinMode(LORA_RST, OUTPUT);
    digitalWrite(LORA_RST, LOW);  delay(10);
    digitalWrite(LORA_RST, HIGH); delay(10);
    SPI.begin();

    rtcOk = rtcLiteRead();
    if (!rtcOk) DBGLN(F("[RTC] missing"));

    lcd_init();

    esp01_init();

    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

    uint8_t tries = 0;
    bool up = false;
    while (!(up = LoRa.begin(LORA_FREQ))) {
        DBGLN(F("[LoRa] init failed"));
        digitalWrite(LORA_RST, LOW);  delay(50);
        digitalWrite(LORA_RST, HIGH); delay(200);
        if (++tries > 10) { DBGLN(F("[LoRa] OFFLINE")); break; }
    }
    if (up) {
        LoRa.enableCrc();
        LoRa.setSyncWord(LORA_SYNC_WORD);
        LoRa.setTxPower(LORA_TX_POWER);
        LoRa.setSpreadingFactor(SF_RESCUE);
        LoRa.receive();
    }

    bool factory = false;
    loadMeter(factory);
    if (factory) lcd_overlay("NEW METER", "Bal 0.00");

    attachInterrupt(digitalPinToInterrupt(POWER_SENSE_PIN), runGaspISR, GASP_EDGE);

    emon.voltage(VOLT_PIN, VOLTAGE_CAL, VOLTAGE_PHASE);
    emon.current(CURR_PIN, CURRENT_CAL);

    lastBillMs = millis();
    tPower = tLcd = tLora = millis();

    wdt_enable(WATCHDOG_TIMEOUT);

    DBG(F("ONLINE bal="));  DBGF(mpToInr(balance_mp), 5);
    DBG(F(" SF="));         DBGLN(currentSF);
#if ENABLE_CONSOLE
    Serial.println(F("Console: STATUS|CAL|TESTRECH:<amt>|SETTIME:<...>"));
#endif
}

// ===========================================================================
void loop() {
    handleGasp();
    wdt_reset();

    unsigned long now = millis();

    handleIncoming();
    lcd_service();

    if (now - tPower >= POWER_INTERVAL_MS) {
        tPower = now;
        processPower();

        // Shadow clock refresh. rtcLiteRead() returns false only when the chip
        // does not ACK, so "missing" and "never set" stay distinguishable --
        // one needs a soldering iron, the other a serial command.
        bool ok = rtcLiteRead();
        if (ok != rtcOk) {
            rtcOk = ok;
            DBGLN(ok ? F("[RTC] up") : F("[RTC] down"));
        }
        bool set = ok && !rtcLiteLostPower() && rtcYear >= 2024 && rtcYear <= 2099;
        if (set != rtcTimeSet) {
            rtcTimeSet = set;
            if (!set) DBGLN(F("[RTC] NOT SET"));
        }
    }

    if (now - tLcd >= LCD_INTERVAL_MS) { tLcd = now; lcd_update(); }

    if (now - tLora >= TELEMETRY_INTERVAL_MS) {
        if (sendTelemetry()) tLora = millis();
    }

#if ENABLE_CONSOLE
    console();
#endif
    checkpointIfNeeded();
}