#include "power_measure.h"
#include "config.h"
#include "globals.h"
#include "powerManagement.h"
#include <EmonLib.h>
#include <avr/wdt.h>

// Undivided cost numerator. One 3-second window at a small load costs a
// FRACTION of a milli-paisa; rounding each window to a whole unit would round it
// to zero every time. Keeping the remainder here makes billing exact over any
// timespan. Worst case ~2.4e17, which is why it must be int64.
static int64_t costAccum = 0;

void processPower() {
    wdt_reset();
    emon.calcVI(20, 2000);      // blocks up to the timeout if V never crosses zero
    wdt_reset();

    Voltage = emon.Vrms;
    Current = emon.Irms;
    Power   = emon.realPower;
    PF      = emon.powerFactor;

    // Negative real power means a reversed sensor or wrong VOLTAGE_PHASE.
    // The original code hid this with fabs(), which also billed miscalibrated
    // garbage as real consumption. Refuse to bill it and say so instead.
    if (Power < 0.0f) {
        DBGLN(F("[WARN] neg power: check sensor dir / VOLTAGE_PHASE"));
        Power = 0.0f;
        PF    = 0.0f;
    }
    if (PF < 0.0f) PF = 0.0f;
    if (PF > 1.0f) PF = 1.0f;

    if (Current < MIN_CURRENT_THRESHOLD) { Current = 0; Power = 0; PF = 0; }

    if (Power > MAX_PLAUSIBLE_POWER_W) {
        DBG(F("[WARN] implausible P ignored: "));
        DBGF(Power, 1);
        DBGLN(F(""));
        Power = 0.0f;
    }

    // Measured elapsed time, not assumed. If the loop stalled we still bill the
    // real energy rather than pretending only 3 seconds passed.
    unsigned long now  = millis();
    unsigned long dtMs = now - lastBillMs;
    lastBillMs = now;
    if (dtMs == 0) return;
    if (dtMs > MAX_WINDOW_MS) dtMs = MAX_WINDOW_MS;

    // cost_mp = power_mW * dt_ms * tariff_mp / 3.6e12
    if (Power > 0.0f && tariff_mp > 0) {
        costAccum += (int64_t)(int32_t)(Power * 1000.0f)
                   * (int64_t)dtMs
                   * (int64_t)tariff_mp;

        int32_t whole = (int32_t)(costAccum / COST_DIVISOR);
        if (whole > 0) {
            costAccum -= (int64_t)whole * COST_DIVISOR;
            balance_mp -= whole;
            if (balance_mp < 0) balance_mp = 0;
        }
    }

    if (balance_mp <= 0) {
        setRelay(false);
        Current = 0; Power = 0; PF = 0;   // relay open -> no current, genuinely
    } else {
        setRelay(true);
    }
}