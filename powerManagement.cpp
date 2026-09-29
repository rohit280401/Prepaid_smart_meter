#include "powerManagement.h"
#include "config.h"
#include "globals.h"
#include <EEPROM.h>
#include <SPI.h>
#include <LoRa.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>

volatile bool gaspLatched = false;
volatile bool gaspPending = false;

static int32_t       lastSavedBal = 0;
static unsigned long lastSaveMs   = 0;

uint16_t crc16(const uint8_t* d, uint16_t n) {
    uint16_t c = 0xFFFF;
    while (n--) {
        c ^= (uint16_t)(*d++) << 8;
        for (uint8_t i = 0; i < 8; i++)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

void setRelay(bool closed) {
    if (gaspLatched && closed) return;          // blackout interlock
    digitalWrite(RELAY_PIN, closed ? RELAY_CLOSED : RELAY_OPEN);
    loadState = closed;
}

bool saveMeter(bool quiet) {
    MeterRecord r;
    r.magic      = EE_MAGIC;
    r.balance_mp = balance_mp;
    r.tariff_mp  = tariff_mp;
    r.lastTxId   = lastTxId;
    r.sf         = currentSF;
    r.pad        = 0;
    r.crc        = crc16((const uint8_t*)&r, sizeof(r) - 2);

    EEPROM.put(EE_ADDR, r);                     // put() skips unchanged bytes

    lastSavedBal = balance_mp;
    lastSaveMs   = millis();

#if ENABLE_DEBUG
    if (!quiet) {
        DBG(F("[EE] bal="));   DBGF(mpToInr(balance_mp), 5);
        DBG(F(" txid="));      DBGLN(lastTxId);
    }
#else
    (void)quiet;
#endif
    return true;
}

void checkpointIfNeeded() {
    // Without this, any reset that is not a clean gasp -- brownout, USB unplug,
    // watchdog, reset button -- rolls the balance back to the last recharge.
    if ((millis() - lastSaveMs) < CHECKPOINT_MIN_MS) return;
    int32_t d = balance_mp - lastSavedBal;
    if (d < 0) d = -d;
    if (d < CHECKPOINT_MIN_DELTA) return;
    saveMeter(true);
}

bool loadMeter(bool &factoryReset) {
    factoryReset = false;
    MeterRecord r;
    EEPROM.get(EE_ADDR, r);

    bool valid = (r.magic == EE_MAGIC) &&
                 (crc16((const uint8_t*)&r, sizeof(r) - 2) == r.crc);

    if (valid) {
        // Clamp out-of-range fields rather than discarding the record. These
        // bounds are CONFIGURATION; the CRC already covers corruption. Rejecting
        // on a bounds change means editing a #define can delete a balance.
        if (r.balance_mp < 0)              r.balance_mp = 0;
        if (r.balance_mp > BALANCE_MAX_MP) r.balance_mp = BALANCE_MAX_MP;
        if (r.tariff_mp < TARIFF_MIN_MP || r.tariff_mp > TARIFF_MAX_MP)
            r.tariff_mp = TARIFF_DEFAULT_MP;
        if (r.sf < SF_MIN || r.sf > SF_MAX) r.sf = SF_RESCUE;

        balance_mp = r.balance_mp;
        tariff_mp  = r.tariff_mp;
        lastTxId   = r.lastTxId;
        currentSF  = r.sf;
        currentSF = 9;

        DBG(F("[EE] restored "));
        DBGF(mpToInr(balance_mp), 5);
        DBGLN(F(""));
    } else {
        // Blank or corrupt. Default balance is ZERO, not Rs 10 -- a brand new
        // meter has never been recharged.
        factoryReset = true;
        balance_mp = 0;
        tariff_mp  = TARIFF_DEFAULT_MP;
        lastTxId   = 0;
        currentSF  = SF_RESCUE;
        DBGLN(F("[EE] FACTORY RESET, bal 0"));
        saveMeter(true);
    }

    lastSavedBal = balance_mp;
    lastSaveMs   = millis();

    LoRa.setSpreadingFactor(currentSF);
    bool credit = (balance_mp > 0);
    setRelay(credit);
    return credit;
}

// --- ISR: microseconds only. No Serial, no LoRa, no EEPROM, no delay. -------
void runGaspISR() {
    if (gaspLatched) return;
    gaspLatched = true;
    detachInterrupt(digitalPinToInterrupt(POWER_SENSE_PIN));
    digitalWrite(RELAY_PIN, RELAY_OPEN);        // cut load: safety + saves charge
    gaspPending = true;
}

void handleGasp() {
    if (!gaspPending) return;
    gaspPending = false;

    // Confirm across more than one full mains cycle. An 8 ms window is shorter
    // than a 10 ms half-cycle, so an unfiltered sense line makes every
    // zero-crossing look like an outage and the relay chatters.
    uint8_t absent = 0;
    for (uint8_t i = 0; i < GASP_SAMPLES; i++) {
        delay(GASP_CONFIRM_MS / GASP_SAMPLES);
        wdt_reset();
        if (digitalRead(POWER_SENSE_PIN) != MAINS_PRESENT_LEVEL) absent++;
    }

    if (absent < GASP_SAMPLES) {
        gaspLatched = false;
        attachInterrupt(digitalPinToInterrupt(POWER_SENSE_PIN), runGaspISR, GASP_EDGE);
        setRelay(balance_mp > 0);
        // If this repeats, the sense line needs an RC filter:
        //   1N4148 -> 100k -> node; node -> 10uF to GND;
        //   node -> 100k to GND; node -> pin 3.
        DBGLN(F("[GASP] false alarm"));
        return;
    }

    // EEPROM FIRST. A LoRa transmit costs ~8x the charge of the EEPROM write
    // (20 mC vs 2.2 mC), so if the capacitor gives out it gives out during the
    // transmit -- and the original ordering took the balance down with it.
    lcd.noBacklight();                          // shed 20-40 mA immediately
    saveMeter(true);

    // Transmit at the CURRENT link SF: that is where the gateway is listening.
    char buf[56], bal[12];
    dtostrf(mpToInr(balance_mp), 1, 2, bal);
    snprintf_P(buf, sizeof(buf), PSTR("<%s;GASP;ID:%lu;BAL:%s>"),
               NODE_ID, (unsigned long)lastTxId, bal);
    LoRa.beginPacket();
    LoRa.print(buf);
    LoRa.endPacket();
    LoRa.sleep();

    // Wait for mains to return rather than resetting blindly. With a large
    // holdup bank an unconditional reset reboots into a dead grid and burns the
    // whole reserve on LoRa retries and the backlight.
    wdt_disable();
    uint8_t run = 0;
    while (1) {
        if (digitalRead(POWER_SENSE_PIN) == MAINS_PRESENT_LEVEL) {
            if (++run >= 20) { wdt_enable(WDTO_15MS); while (1) {} }  // clean reset
        } else run = 0;
        delay(5);
    }
}