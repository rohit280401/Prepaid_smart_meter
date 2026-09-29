#include "lora_ops.h"
#include "config.h"
#include "globals.h"
#include "powerManagement.h"
#include "lcd_display.h"
#include "esp01_ops.h"
#include <LoRa.h>
#include <string.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>

static char rx[80];

static bool          waitingAck = false;
static unsigned long txTime     = 0;
static uint8_t       txSF       = SF_RESCUE;

// --- Fallback state ---------------------------------------------------------
// httpMode is deliberately NOT persisted. A reboot should always start by
// trying LoRa: the reason for the reboot may be the reason the link was down.
static bool httpMode = false;

// ---------------------------------------------------------------------------
//  TWO SEPARATE NOTIONS OF SF
//
//    currentSF -- the AGREED spreading factor. Persisted in EEPROM. Only
//                 changes on a gateway command, a deliberate escalation, or
//                 adoption after a successful rendezvous.
//    radioSF   -- what the chip is actually tuned to right now. While in HTTP
//                 fallback this is parked at SF_RESCUE regardless of currentSF.
//
//  Keeping them separate is what lets the meter sit on the rendezvous channel
//  without losing its stored preference, without an EEPROM write, and without
//  making the `currentSF >= SF_HTTP_THRESHOLD` fallback condition unreachable.
//  v6 deleted the old applySF(SF_RESCUE) rescue because it clobbered all three;
//  this restores the behaviour without the side effects.
// ---------------------------------------------------------------------------
static uint8_t radioSF = SF_RESCUE;

static void setRadioSF(uint8_t s) {
    if (s == radioSF) return;
    radioSF = s;
    LoRa.setSpreadingFactor(s);
    LoRa.receive();
}

bool loraHttpMode() { return httpMode; }

static void enterHttpMode() {
    if (httpMode) return;
    httpMode = true;
    // Park on the rendezvous channel and STAY there. The gateway's sweep
    // revisits SF_RESCUE every other dwell, so a probe every telemetry cycle
    // is guaranteed to land inside one of those windows. Alternating probe SFs
    // (v7) made this a coincidence instead of a certainty.
    setRadioSF(SF_RESCUE);
    DBGLN(F("[LINK] -> HTTP, parked SF7"));
}

static void linkRecovered() {
    // Whatever SF we were tuned to when the gateway answered is the SF that
    // works. Adopt and commit it -- including dropping back to SF_RESCUE after
    // a rendezvous, which is correct: the gateway's auto-SF will raise it again
    // if the link genuinely needs a higher one.
    if (radioSF != currentSF) {
        currentSF = radioSF;
        saveMeter(true);
        DBG(F("[SF] adopted ")); DBGLN(currentSF);
    }
    if (httpMode) {
        httpMode = false;
        DBGLN(F("[LINK] LoRa back"));
    }
    waitingAck = false;
    ackMisses  = 0;
}

static inline uint8_t sfIdx(uint8_t sf) {
    if (sf < SF_MIN) return 0;
    if (sf > SF_MAX) return SF_MAX - SF_MIN;
    return sf - SF_MIN;
}

static bool field(const char* pkt, const char* key, char* out, uint8_t n) {
    const char* s = strstr(pkt, key);
    if (!s) return false;
    s += strlen(key);
    uint8_t i = 0;
    while (i < n - 1 && *s != ';' && *s != '>' && *s) out[i++] = *s++;
    out[i] = 0;
    return i > 0;
}

static bool forUs(const char* p) {
    return p[0] == '<' && strncmp(p + 1, NODE_ID, NODE_ID_LEN) == 0
           && p[1 + NODE_ID_LEN] == ';';
}

static bool isType(const char* p, const char* t) {
    return strncmp(p + 2 + NODE_ID_LEN, t, strlen(t)) == 0;
}

void applySF(uint8_t sf) {
    if (sf < SF_MIN || sf > SF_MAX) return;
    bool changed = (sf != currentSF);
    currentSF = sf;
    setRadioSF(sf);                 // always retune, even when currentSF matched:
                                    // the radio may be parked on the rendezvous SF
    if (changed) {
        saveMeter(true);
        DBG(F("[SF] ")); DBGLN(sf);
    }
}

void sendAck(uint32_t txid, const char* status) {
    LoRa.beginPacket();
    LoRa.print(F("<"));
    LoRa.print(NODE_ID);
    LoRa.print(F(";ACK;ID:"));
    LoRa.print(txid);
    LoRa.print(F(";ST:"));
    LoRa.print(status);
    LoRa.print(F(";SF:"));
    LoRa.print(radioSF);            // what we are ACTUALLY on, not what we prefer
    LoRa.print(F(">"));
    LoRa.endPacket();
    LoRa.receive();
}

void handleIncoming() {
    int n = LoRa.parsePacket();
    if (n <= 0) return;

    int i = 0;
    while (LoRa.available()) {
        int c = LoRa.read();
        if (i < (int)sizeof(rx) - 1) rx[i++] = (char)c;
    }
    rx[i] = 0;

    if (i < 8 || !forUs(rx) || rx[i - 1] != '>') { LoRa.receive(); return; }

    // Any well-formed packet addressed to us proves the link works in both
    // directions -- the gateway heard us well enough to answer. Strongest
    // recovery signal available, so it runs before any parsing.
    linkRecovered();

    DBG(F("[RX] ")); DBGLN(rx);
    char v[16];

    if (isType(rx, "TAK"))  { LoRa.receive(); return; }
    if (!isType(rx, "CMD")) { LoRa.receive(); return; }

    if (!field(rx, ";ID:", v, sizeof(v))) { LoRa.receive(); return; }
    uint32_t id = (uint32_t)strtoul(v, NULL, 10);

    if (id <= lastTxId) {
        sendAck(id, "DUP");
        LoRa.receive();
        return;
    }
    lastTxId = id;

    if (field(rx, ";RECH:", v, sizeof(v))) {
        int32_t amt = inrToMp(atof(v));
        if (amt > 0) {
            int64_t s = (int64_t)balance_mp + amt;
            if (s > BALANCE_MAX_MP) s = BALANCE_MAX_MP;
            balance_mp = (int32_t)s;
            saveMeter(false);
            lcd_rechargeOk(balance_mp);
            sendAck(id, "RECH_OK");
        }
    }
    else if (field(rx, ";TARI:", v, sizeof(v))) {
        int32_t r = inrToMp(atof(v));
        if (r >= TARIFF_MIN_MP && r <= TARIFF_MAX_MP) {
            tariff_mp = r;
            saveMeter(false);
            sendAck(id, "TARI_OK");
        }
    }
    else if (field(rx, ";SF:", v, sizeof(v))) {
        // ACK FIRST, at the OLD SF -- that is where the gateway is still
        // listening until it sees our reply. Only then switch.
        sendAck(id, "SF_OK");
        applySF((uint8_t)atoi(v));
    }
    else {
        saveMeter(true);
        sendAck(id, "UNKNOWN");
    }
    LoRa.receive();
}

// Streams one telemetry packet into the LoRa FIFO and arms the ACK timer.
static void loraTx() {
    wdt_reset();
    LoRa.beginPacket();
    LoRa.print(F("<"));
    LoRa.print(NODE_ID);
    LoRa.print(F(";TLM;ID:"));
    LoRa.print(lastTxId);
    LoRa.print(F(";V:"));
    LoRa.print(Voltage, 1);
    LoRa.print(F(";I:"));
    LoRa.print(Current, 3);
    LoRa.print(F(";P:"));
    LoRa.print(Power, 1);
    LoRa.print(F(";PF:"));
    LoRa.print(PF, 2);
    LoRa.print(F(";BAL:"));
    LoRa.print(mpToInr(balance_mp), 2);
    LoRa.print(F(";RT:"));
    LoRa.print(mpToInr(tariff_mp), 2);
    LoRa.print(F(";SF:"));
    // ADVERTISE THE SF WE ACTUALLY SENT AT. Advertising currentSF while
    // transmitting on the rendezvous channel made the gateway retune away from
    // us before it replied -- its ACK went out at SF9 while we sat on SF7.
    LoRa.print(radioSF);
    LoRa.print(F(">"));
    LoRa.endPacket();
    wdt_reset();
    LoRa.receive();

    waitingAck = true;
    txTime     = millis();
    txSF       = radioSF;
}

bool sendTelemetry() {
    unsigned long now = millis();

    // ---- 1. Judge the outstanding ACK, if any -----------------------------
    if (waitingAck) {
        uint16_t timeout = pgm_read_word(&(ACK_TIMEOUT_MS[sfIdx(txSF)]));
        if ((now - txTime) < timeout) return false;   // still within window

        waitingAck = false;

        if (httpMode) {
            // A failed rendezvous probe. Stay parked on SF_RESCUE and stay on
            // Wi-Fi. No escalation: currentSF is at or above the threshold by
            // definition of being here.
            setRadioSF(SF_RESCUE);
        } else {
            ackMisses++;
            DBG(F("[LINK] miss ")); DBGLN(ackMisses);

            if (ackMisses >= ACK_MISS_LIMIT) {
                ackMisses = 0;
                if (currentSF >= SF_HTTP_THRESHOLD) {
                    enterHttpMode();
                } else {
                    // Climb the ladder first: a higher SF reaches further and
                    // costs far less power than the Wi-Fi radio.
                    uint8_t next = currentSF + SF_ESCALATE_STEP;
                    if (next > SF_MAX) next = SF_MAX;
                    applySF(next);
                }
            }
        }
    }

    // ---- 2. HTTP mode -----------------------------------------------------
    if (httpMode) {
        // Probe on EVERY cycle, always on SF_RESCUE. One short packet costs
        // ~50 ms of airtime; making rendezvous deterministic is worth far more
        // than that. The gateway sweep sits on SF_RESCUE for 20 s out of every
        // 40 s, and we probe every 15 s, so a probe cannot miss the window.
        setRadioSF(SF_RESCUE);
        DBGLN(F("[LINK] probe SF7"));
        loraTx();
        return esp01_sendHttpTelemetry();
    }

    // ---- 3. Normal LoRa path ----------------------------------------------
    setRadioSF(currentSF);
    loraTx();
    DBGLN(F("[TX] tlm"));
    return true;
}