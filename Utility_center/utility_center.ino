/*
 * ============================================================================
 *  WIRELESS PREPAID ENERGY METER  --  UTILITY GATEWAY  v7
 * ============================================================================
 *  Board: Arduino Uno/Nano + SX1276.  Library: arduino-LoRa (sandeepmistry)
 *
 *  Serial console (9600):
 *      RECH:500        credit Rs 500 to the meter
 *      TARI:8.50       set tariff, Rs per kWh
 *      SF:9            ask the METER to change spreading factor (disables auto)
 *      LSF:9           set THIS radio's SF locally + EEPROM. Sends nothing.
 *      AUTO:ON|OFF     automatic SF adaptation
 *      RAW:ON|OFF      dump every received packet
 *      SCAN            force an SF rescan now
 *      EEWIPE          clear the stored SF hint
 *      STATUS          link and command state
 *
 *  ==========================================================================
 *  v7 -- THE SF HANDSHAKE WAS PASSING IN OPPOSITE DIRECTIONS
 *  ==========================================================================
 *  Meter v7/v8 sends its SF_OK acknowledgement at the OLD SF and switches
 *  afterwards. This gateway transmitted at the old SF and then immediately
 *  moved to the NEW one -- so it was deaf to every confirmation it had just
 *  asked for. Symptom: every SF change failed on both retries and reverted.
 *
 *      [TX] <Node1;CMD;ID:6;SF:7>  (try 1)
 *      [RETRY]
 *      [TX] <Node1;CMD;ID:6;SF:7>  (try 2)
 *      *** FAILED: SF:7 (id 6) ***
 *
 *  FIX: stay at the OLD SF until the ACK arrives, then switch. This is also
 *  strictly safer than the optimistic switch: if the meter never received the
 *  command it stays put and keeps sending telemetry at the old SF, where we
 *  are still listening. Nothing gets orphaned.
 *
 *  ALSO FIXED IN v7
 *  ----------------
 *  DUP WAS TREATED AS SUCCESS. If our txCounter was stale, the meter rejected
 *  the command as a replay and returned ST:DUP -- and we printed SUCCESS for a
 *  command that never executed. The same log above shows the gateway sending
 *  ID:6 while the meter's high-water mark was ALREADY 6, so that command was
 *  dead on arrival for a second, independent reason. DUP is now a failure, and
 *  the counter resyncs from the meter's next telemetry packet.
 *
 *  AUTO-SF THRASHING. A failed SF change was retried every SF_COOLDOWN_MS
 *  forever, yanking the radio off-channel for ~2.4 s each time and creating
 *  gaps in telemetry reception. Three consecutive failures now disable auto-SF
 *  and say so.
 *
 *  EARLIER FIXES (still true)
 *  --------------------------
 *  RENDEZVOUS. SF_RESCUE is interleaved into the sweep, so a meter parked on
 *  the rendezvous channel is heard within ~40 s rather than by luck.
 *  ACK BEFORE RETUNE in the telemetry path.
 *  RESET DEADLOCK. EEPROM-persisted SF hint + sweep while unsynced.
 *  SF announcement ordering in transmit().
 *  Retries on commands; per-SF timeouts; hardware CRC on; watchdog, no while(1).
 *
 *  KNOWN LIMITATION: packets are not authenticated. The monotonic transaction
 *  id blocks replay, not forgery.
 * ============================================================================
 */

#include <SPI.h>
#include <LoRa.h>
#include <EEPROM.h>
#include <string.h>
#include <avr/wdt.h>

#define LORA_SS   10
#define LORA_RST  7
#define LORA_DIO0 2

#define NODE_ID     "Node1"
#define NODE_ID_LEN 5

const long LORA_FREQ = 865E6;
#define LORA_SYNC_WORD 0x3B     // MUST match the meter
#define LORA_TX_POWER  17

#define SF_MIN 7
#define SF_MAX 12
#define SF_RESCUE 7             // MUST match the meter. Rendezvous channel.

const uint16_t ACK_TIMEOUT_MS[6] = { 1200, 1600, 2400, 3600, 6000, 10000 };

#define SF_COOLDOWN_MS 60000UL
#define VOTES 3
#define MAX_RETRIES 5
#define SF_RETRIES  3           // was 2. The handshake is now reliable, so a
                                // third try costs little and helps at high SF.
#define SF_FAIL_LIMIT 3         // consecutive SF failures before auto-SF is off
#define SILENCE_MS  60000UL
#define HEARTBEAT_MS 30000UL
#define WATCHDOG_TIMEOUT WDTO_8S

#define SCAN_DWELL_MS 20000UL   // must exceed the meter's telemetry interval

#define EE_GW_ADDR  0
#define EE_GW_MAGIC 0x5A

static char rx[200], tx[180], cmd[64];
static uint8_t clen = 0;

static uint8_t  currentSF = SF_RESCUE;
static bool     autoSF = true, rawDbg = true, synced = false;
static uint32_t txCounter = 0, seen = 0;
static unsigned long lastHeard = 0, lastSfChange = 0, lastBeat = 0;
static uint8_t  sfFailStreak = 0;

static uint8_t  scanOrder[16];
static uint8_t  scanLen = 0, scanIdx = 0;
static unsigned long lastScan = 0;

static uint8_t votes[VOTES], vIdx = 0, vCount = 0;

static bool     pending = false, pendIsSF = false;
static char     pendBody[40];
static uint32_t pendId = 0;
static uint8_t  pendTries = 0, pendSFnew = 7, pendSFold = 7;
static unsigned long pendTime = 0;

static bool haveHeld = false, heldIsSF = false;
static char heldBody[40];
static uint8_t heldSF = 7;

static inline uint8_t sfIdx(uint8_t s) {
    if (s < SF_MIN) return 0;
    if (s > SF_MAX) return SF_MAX - SF_MIN;
    return s - SF_MIN;
}

// ---------------------------------------------------------------------------
//  setRadioSF() -- retune the chip only. Used while sweeping. No commitment.
//  setSF()      -- retune AND record this as the agreed SF, in EEPROM.
// ---------------------------------------------------------------------------
static void setRadioSF(uint8_t s) {
    currentSF = s;
    LoRa.setSpreadingFactor(s);
}

static void saveSF(uint8_t s) {
    EEPROM.update(EE_GW_ADDR,     EE_GW_MAGIC);
    EEPROM.update(EE_GW_ADDR + 1, s);
}

static uint8_t loadSF() {
    if (EEPROM.read(EE_GW_ADDR) != EE_GW_MAGIC) return SF_RESCUE;
    uint8_t s = EEPROM.read(EE_GW_ADDR + 1);
    return (s >= SF_MIN && s <= SF_MAX) ? s : SF_RESCUE;
}

static void setSF(uint8_t s) {
    setRadioSF(s);
    saveSF(s);
}

// Sweep: last known SF first, then every SF with SF_RESCUE interleaved, so a
// meter parked on the rendezvous channel never waits a full sweep.
static void buildScan(uint8_t hint) {
    scanLen = 0;
    scanOrder[scanLen++] = hint;
    for (uint8_t s = SF_MIN; s <= SF_MAX; s++) {
        scanOrder[scanLen++] = s;
        if (s != SF_RESCUE) scanOrder[scanLen++] = SF_RESCUE;
    }
    scanIdx = 0;
}

static void startScan() {
    buildScan(currentSF);
    lastScan = millis();
    vCount = 0;
    Serial.print(F("[SCAN] sweep, SF")); Serial.print(SF_RESCUE);
    Serial.print(F(" interleaved, dwell "));
    Serial.print(SCAN_DWELL_MS / 1000); Serial.println(F("s"));
}

static bool field(const char* p, const char* k, char* o, uint8_t n) {
    const char* s = strstr(p, k);
    if (!s) return false;
    s += strlen(k);
    uint8_t i = 0;
    while (i < n - 1 && *s != ';' && *s != '>' && *s) o[i++] = *s++;
    o[i] = 0;
    return i > 0;
}
static bool fromMeter(const char* p) {   // positional: "Node10" also matched
    return p[0] == '<' && !strncmp(p + 1, NODE_ID, NODE_ID_LEN)
           && p[1 + NODE_ID_LEN] == ';';
}
static bool isType(const char* p, const char* t) {
    return !strncmp(p + 2 + NODE_ID_LEN, t, strlen(t));
}

static void transmit() {
    snprintf(tx, sizeof(tx), "<%s;CMD;ID:%lu;%s>",
             NODE_ID, (unsigned long)pendId, pendBody);

    wdt_reset();
    LoRa.beginPacket();
    LoRa.print(tx);
    LoRa.endPacket();
    wdt_reset();

    // ---- STAY PUT ----
    // The meter sends SF_OK at the OLD SF and switches afterwards, so the
    // confirmation arrives in the language we just spoke. Moving to pendSFnew
    // here -- which is what v4..v6 did -- made us deaf to every SF ACK we
    // asked for, and every SF change failed on both retries.
    LoRa.receive();
    pendTime = millis();

    Serial.print(F("[TX] ")); Serial.print(tx);
    Serial.print(F("  (try ")); Serial.print(pendTries + 1);
    Serial.print(F(", listening SF")); Serial.print(currentSF);
    Serial.println(')');
}

static void start(const char* body, bool isSF, uint8_t sfNew) {
    txCounter++;
    pendId = txCounter;
    snprintf(pendBody, sizeof(pendBody), "%s", body);
    pendIsSF = isSF; pendSFnew = sfNew; pendSFold = currentSF;
    pendTries = 0; pending = true;
    transmit();
}

static bool queue(const char* body, bool isSF, uint8_t sfNew) {
    if (pending)  { Serial.println(F("[BUSY] command in flight")); return false; }
    if (!synced) {
        if (haveHeld) { Serial.println(F("[BUSY] command already held")); return false; }
        snprintf(heldBody, sizeof(heldBody), "%s", body);
        heldIsSF = isSF; heldSF = sfNew; haveHeld = true;
        Serial.print(F("[HELD] ")); Serial.print(heldBody);
        Serial.println(F(" -- sends when the meter is heard"));
        return true;
    }
    start(body, isSF, sfNew);
    return true;
}

static void finish(bool ok) {
    Serial.print(ok ? F("\n*** SUCCESS: ") : F("\n*** FAILED: "));
    Serial.print(pendBody);
    Serial.print(F(" (id ")); Serial.print(pendId); Serial.println(F(") ***\n"));

    if (pendIsSF) {
        if (ok) sfFailStreak = 0;
        else if (++sfFailStreak >= SF_FAIL_LIMIT && autoSF) {
            // Do not keep yanking the radio off-channel once a minute forever.
            // Each attempt costs ~2.4 s of deafness and creates telemetry gaps.
            autoSF = false;
            Serial.println(F("[SF] 3 failures -- auto SF DISABLED. Fix the link, then AUTO:ON"));
        }
    }
    // No SF revert needed: we never left the old SF. That is the point of v7.
    pending = false;
}

static void service() {
    if (!pending) return;
    // We are sitting at the OLD SF for the whole exchange, so the timeout is
    // simply that SF's round trip.
    if ((millis() - pendTime) < ACK_TIMEOUT_MS[sfIdx(currentSF)]) return;

    if (pendTries + 1 >= (pendIsSF ? SF_RETRIES : MAX_RETRIES)) { finish(false); return; }
    pendTries++;
    Serial.println(F("[RETRY]"));
    transmit();
}

static uint8_t recommend(int rssi, float snr) {
    uint8_t s;
    if      (rssi >= -85)  s = 7;
    else if (rssi >= -95)  s = 8;
    else if (rssi >= -105) s = 9;
    else if (rssi >= -115) s = 10;
    else if (rssi >= -120) s = 11;
    else                   s = 12;
    if (snr >= 8.0f && s > SF_MIN) s--;
    else if (snr <= -5.0f && s < SF_MAX) s++;
    return s;
}

static void maybeChangeSF(int rssi, float snr) {
    if (!autoSF || pending) return;
    if ((millis() - lastSfChange) < SF_COOLDOWN_MS) return;

    votes[vIdx] = recommend(rssi, snr);
    vIdx = (vIdx + 1) % VOTES;
    if (vCount < VOTES) vCount++;
    if (vCount < VOTES) return;
    for (uint8_t i = 1; i < VOTES; i++) if (votes[i] != votes[0]) return;
    if (votes[0] == currentSF) return;

    char b[12];
    snprintf(b, sizeof(b), "SF:%u", (unsigned)votes[0]);
    if (queue(b, true, votes[0])) { lastSfChange = millis(); vCount = 0; }
}

static void ackTelemetry(uint32_t id) {
    snprintf(tx, sizeof(tx), "<%s;TAK;ID:%lu>", NODE_ID, (unsigned long)id);
    LoRa.beginPacket(); LoRa.print(tx); LoRa.endPacket(); LoRa.receive();
    Serial.print(F("[TX] ")); Serial.println(tx);
}

static void incoming() {
    int n = 0;
    while (LoRa.available()) { int c = LoRa.read(); if (n < (int)sizeof(rx)-1) rx[n++] = (char)c; }
    rx[n] = 0;

    int rssi = LoRa.packetRssi();
    float snr = LoRa.packetSnr();
    seen++; lastHeard = millis();

    if (rawDbg) {
        Serial.print(F("[RAW] ")); Serial.print(n);
        Serial.print(F("B SF")); Serial.print(currentSF);
        Serial.print(F(" RSSI ")); Serial.print(rssi);
        Serial.print(F(" SNR ")); Serial.print(snr, 1);
        Serial.print(F(" | ")); Serial.println(rx);
    }
    if (n < 8 || !fromMeter(rx) || rx[n-1] != '>') { LoRa.receive(); return; }

    char v[24];

    // ======================= ACK =========================================
    if (isType(rx, "ACK")) {
        uint32_t id = field(rx, ";ID:", v, sizeof(v)) ? strtoul(v, NULL, 10) : 0;
        char st[16] = ""; field(rx, ";ST:", st, sizeof(st));

        if (pending && id == pendId) {
            if (!strcmp(st, "DUP")) {
                // The meter's high-water mark was already >= our id, so it
                // rejected the command outright. v6 called this SUCCESS, which
                // reported a recharge that never happened. Our counter is
                // stale; the meter's next telemetry packet resyncs it.
                Serial.println(F("[DUP] meter rejected: stale id. Resync then resend."));
                if (id > txCounter) txCounter = id;
                finish(false);
            }
            else if (!strcmp(st, "UNKNOWN")) {
                finish(false);
            }
            else if (pendIsSF) {
                if (!strcmp(st, "SF_OK")) {
                    // Confirmed. NOW switch -- the meter switches right after
                    // sending this, so both sides move together.
                    setSF(pendSFnew);
                    LoRa.receive();
                    lastSfChange = millis();
                    Serial.print(F("[SF] confirmed, now SF")); Serial.println(currentSF);
                    finish(true);
                } else {
                    finish(false);
                }
            }
            else {
                finish(true);
            }
            LoRa.receive(); return;
        }

        // Unsolicited ACK (or one for a command we already gave up on). Its
        // ;SF: field reports where the meter's radio actually is -- follow it.
        if (field(rx, ";SF:", v, sizeof(v))) {
            uint8_t r = atoi(v);
            if (r >= SF_MIN && r <= SF_MAX && r != currentSF) { setSF(r); }
        }
        LoRa.receive(); return;
    }

    if (isType(rx, "GASP")) {
        Serial.print(F("\n!!! BLACKOUT: ")); Serial.println(rx);
        LoRa.receive(); return;
    }

    if (!isType(rx, "TLM")) { LoRa.receive(); return; }

    // ======================= TELEMETRY ===================================
    Serial.print(F("[RX] ")); Serial.println(rx);
    Serial.print(F("     RSSI ")); Serial.print(rssi);
    Serial.print(F(" dBm  SNR ")); Serial.println(snr, 1);

    uint32_t id = field(rx, ";ID:", v, sizeof(v)) ? strtoul(v, NULL, 10) : 0;

    // The meter reports its high-water mark. Our counter must sit strictly
    // above it or our next command is rejected as a replay. This is how the
    // gateway recovers its counter after a reboot: the meter is authoritative.
    if (id > txCounter) { txCounter = id; Serial.print(F("[SYNC] txid -> ")); Serial.println(id); }

    bool justSynced = false;
    if (!synced) {
        synced = true; justSynced = true;
        saveSF(currentSF);      // commit the SF the sweep landed on
        Serial.print(F("[SYNC] meter online at SF")); Serial.println(currentSF);
    }

    // ---- ACK FIRST, RETUNE SECOND -----------------------------------------
    // The meter is listening on the SF it just transmitted on. Reply there
    // before touching the radio.
    ackTelemetry(id);

    if (field(rx, ";SF:", v, sizeof(v))) {
        uint8_t r = atoi(v);
        if (r >= SF_MIN && r <= SF_MAX && r != currentSF) {
            Serial.print(F("[SF] matching meter SF")); Serial.println(r);
            setSF(r);
            LoRa.receive();
        }
    }

    if (haveHeld && !pending) {
        Serial.print(F("[HELD] releasing ")); Serial.println(heldBody);
        haveHeld = false;
        start(heldBody, heldIsSF, heldSF);
        return;
    }
    if (!justSynced) maybeChangeSF(rssi, snr);
}

static void supervise() {
    unsigned long now = millis();

    if (now - lastBeat >= HEARTBEAT_MS) {
        lastBeat = now;
        if (!synced) {
            Serial.print(F("[IDLE] scanning, now SF")); Serial.print(currentSF);
            Serial.print(F("  865MHz  sync 0x")); Serial.print(LORA_SYNC_WORD, HEX);
            Serial.print(F("  packets seen: ")); Serial.println(seen);
            if (!seen) Serial.println(F("       ZERO packets on any SF. Check frequency, sync word, antennas."));
        }
    }

    if (!synced) {
        if (now - lastScan >= SCAN_DWELL_MS) {
            lastScan = now;
            scanIdx = (uint8_t)((scanIdx + 1) % scanLen);
            setRadioSF(scanOrder[scanIdx]);   // radio only -- nothing committed
            LoRa.receive();
            Serial.print(F("[SCAN] SF")); Serial.println(currentSF);
        }
        return;
    }

    if ((now - lastHeard) < SILENCE_MS) return;

    Serial.println(F("\n[LINK] silence -- dropping to scan\n"));
    synced    = false;
    lastHeard = now;
    if (pending && pendIsSF) finish(false);
    startScan();
}

static void doCommand(char* s) {
    int n = strlen(s);
    while (n > 0 && (s[n-1] == '\r' || s[n-1] == ' ')) s[--n] = 0;
    if (!n) return;

    if (!strcasecmp(s, "STATUS")) {
        Serial.print(F("SF=")); Serial.print(currentSF);
        Serial.print(F("  saved=")); Serial.print(loadSF());
        Serial.print(F("  auto=")); Serial.print(autoSF ? F("ON") : F("OFF"));
        Serial.print(F("  sfFails=")); Serial.print(sfFailStreak);
        Serial.print(F("  meter=")); Serial.print(synced ? F("seen") : F("SCANNING"));
        Serial.print(F("  packets=")); Serial.print(seen);
        Serial.print(F("  nextId=")); Serial.print(txCounter + 1);
        Serial.print(F("  held=")); Serial.print(haveHeld ? heldBody : "none");
        Serial.print(F("  pending=")); Serial.println(pending ? pendBody : "none");
        return;
    }
    if (!strcasecmp(s, "SCAN")) {
        synced = false;
        if (pending && pendIsSF) finish(false);
        startScan();
        return;
    }
    if (!strcasecmp(s, "EEWIPE")) {
        EEPROM.update(EE_GW_ADDR, 0xFF);
        Serial.println(F("[EE] cleared -- next boot starts at SF7"));
        return;
    }
    if (!strncasecmp(s, "RAW:", 4))  { rawDbg = !strcasecmp(s+4, "ON");
        Serial.println(rawDbg ? F("raw ON") : F("raw OFF")); return; }
    if (!strncasecmp(s, "AUTO:", 5)) { autoSF = !strcasecmp(s+5, "ON");
        if (autoSF) sfFailStreak = 0;
        Serial.println(autoSF ? F("auto ON") : F("auto OFF")); return; }

    // Local SF: retune THIS radio and write EEPROM. Transmits nothing.
    // Distinct from SF:<n>, which is a command sent TO THE METER and which
    // does nothing at all while unsynced (it just sits in heldBody).
    if (!strncasecmp(s, "LSF:", 4)) {
        int f = atoi(s + 4);
        if (f < SF_MIN || f > SF_MAX) { Serial.println(F("[ERR] LSF 7..12")); return; }
        autoSF = false;
        setSF((uint8_t)f);
        LoRa.receive();
        synced = false;             // we have not actually heard the meter here
        startScan();                // hint is now f, so the sweep starts there
        Serial.print(F("[LSF] SF")); Serial.print(f);
        Serial.println(F(" saved to EEPROM, nothing transmitted"));
        return;
    }

    char b[40];
    if (!strncasecmp(s, "RECH:", 5)) {
        float a = atof(s + 5);
        if (a <= 0 || a > 10000) { Serial.println(F("[ERR] 0.01..10000")); return; }
        char t[16]; dtostrf(a, 1, 2, t);   // dtostrf, not %f -- AVR's snprintf
        snprintf(b, sizeof(b), "RECH:%s", t);   // has NO floating point support
        queue(b, false, currentSF); return;
    }
    if (!strncasecmp(s, "TARI:", 5)) {
        float a = atof(s + 5);
        if (a < 0.10f || a > 50.0f) { Serial.println(F("[ERR] 0.10..50.00")); return; }
        char t[16]; dtostrf(a, 1, 2, t);
        snprintf(b, sizeof(b), "TARI:%s", t);
        queue(b, false, currentSF); return;
    }
    if (!strncasecmp(s, "SF:", 3)) {
        int f = atoi(s + 3);
        if (f < SF_MIN || f > SF_MAX) { Serial.println(F("[ERR] SF 7..12")); return; }
        if ((uint8_t)f == currentSF)  { Serial.println(F("[INFO] already there")); return; }
        autoSF = false;                 // manual override, or the two fight
        sfFailStreak = 0;
        Serial.println(F("[INFO] auto SF disabled"));
        snprintf(b, sizeof(b), "SF:%d", f);
        queue(b, true, (uint8_t)f); return;
    }
    Serial.println(F("[ERR] RECH: | TARI: | SF: | LSF: | AUTO: | RAW: | SCAN | EEWIPE | STATUS"));
}

static void console() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') { cmd[clen] = 0; doCommand(cmd); clen = 0; }
        else if (clen < sizeof(cmd) - 1) cmd[clen++] = c;
    }
}

void setup() {
    wdt_disable();
    Serial.begin(9600);
    delay(50);
    Serial.println(F("\n=== UTILITY GATEWAY v7 ==="));

    pinMode(LORA_RST, OUTPUT);
    digitalWrite(LORA_RST, LOW);  delay(10);
    digitalWrite(LORA_RST, HIGH); delay(10);

    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

    uint8_t tries = 0;
    while (!LoRa.begin(LORA_FREQ)) {
        Serial.println(F("[LoRa] init failed (3.3V supply? SPI wiring?)"));
        delay(500);
        if (++tries > 10) {
            Serial.println(F("[LoRa] unrecoverable -- resetting"));
            wdt_enable(WDTO_15MS); while (1) {}
        }
    }

    LoRa.enableCrc();
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.setTxPower(LORA_TX_POWER);

    uint8_t hint = loadSF();
    setRadioSF(hint);
    LoRa.receive();
    Serial.print(F("[BOOT] last known SF")); Serial.println(hint);

    startScan();
    lastHeard = lastBeat = millis();

    Serial.println(F("RECH: | TARI: | SF: | LSF: | AUTO: | RAW: | SCAN | EEWIPE | STATUS"));
    Serial.println(F("Raw dump is ON. Disable with RAW:OFF\n"));

    wdt_enable(WATCHDOG_TIMEOUT);
}

void loop() {
    wdt_reset();
    if (LoRa.parsePacket() > 0) incoming();
    console();
    service();
    supervise();
}