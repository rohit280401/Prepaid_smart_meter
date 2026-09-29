#include "esp01_ops.h"
#include "config.h"
#include "globals.h"
#include <avr/wdt.h>
#include <avr/pgmspace.h>

// ---------------------------------------------------------------------------
//  This is your working bench sketch, with three changes and one deletion.
//
//  DELETION: String. Nine String temporaries built only to call .length()
//  pull in the whole class (~1.6-2.4 KB of flash). The body is built once
//  with snprintf_P; Content-Length is snprintf_P's return value, so it cannot
//  disagree with the bytes actually sent. Your bench sketch got this right by
//  using json.length(); the earlier firmware used a hand-counted 105 and would
//  have hung the server the moment a field changed width.
//
//  CHANGE 1: timeouts cut from ~21 s to ~4.5 s. In setup() 21 s is harmless.
//  In loop() it is 21 s during which handleGasp() cannot run.
//
//  CHANGE 2: every step checks gaspPending and bails out. Mains failure during
//  a POST now aborts it instead of burning holdup charge on Wi-Fi.
//
//  CHANGE 3: JSON keys match your Node-RED flow exactly (voltage/current/
//  power/pf/balance/rate/last_id/datetime). Short keys would have POSTed fine
//  and produced nulls downstream with no error anywhere.
// ---------------------------------------------------------------------------

#if ESP_ON_HW_SERIAL
  #define ESP Serial
#else
  #include <SoftwareSerial.h>
  static SoftwareSerial espSerial(ESP_RX_PIN, ESP_TX_PIN);
  #define ESP espSerial
#endif

static const char H1[] PROGMEM = "POST ";
static const char H2[] PROGMEM = " HTTP/1.1\r\nHost: ";
static const char H3[] PROGMEM = "\r\nContent-Type: application/json\r\nContent-Length: ";
static const char H4[] PROGMEM = "\r\nConnection: close\r\n\r\n";

#define FS(p) reinterpret_cast<const __FlashStringHelper*>(p)

// All compile-time constants -- the compiler counts these, not a human.
#define HDR_FIXED_LEN ( (sizeof(H1) - 1) + (sizeof(HTTP_PATH) - 1) \
                      + (sizeof(H2) - 1) + (sizeof(HTTP_HOST) - 1) \
                      + (sizeof(H3) - 1) + (sizeof(H4) - 1) )

static char jbuf[JSON_BUF_LEN];

// Returns false if the gasp ISR fired -- caller must abandon the transaction.
static bool espWait(uint16_t ms) {
    unsigned long t = millis();
    while ((uint16_t)(millis() - t) < ms) {
        wdt_reset();
        if (gaspPending) return false;
        while (ESP.available()) {
            int c = ESP.read();
#if ESP_ECHO
            Serial.write((uint8_t)c);
#else
            (void)c;
#endif
        }
    }
    return true;
}

static bool espCmd(const __FlashStringHelper* cmd, uint16_t timeout) {
#if ENABLE_DEBUG && ESP_ECHO
    Serial.print(F("\n> ")); Serial.println(cmd);
#endif
    ESP.println(cmd);
    return espWait(timeout);
}

// Builds the body into jbuf and returns its exact byte count.
static uint16_t buildJson() {
    char v[10], i[10], p[10], f[8], b[14], r[10];
    dtostrf(Voltage, 1, 2, v);
    dtostrf(Current, 1, 3, i);
    dtostrf(Power,   1, 2, p);
    dtostrf(PF,      1, 2, f);
    dtostrf(mpToInr(balance_mp), 1, 2, b);
    dtostrf(mpToInr(tariff_mp),  1, 2, r);

    // A timestamp of all zeros is an honest "the clock is not set", which is
    // more useful downstream than a plausible-looking wrong date.
    char ts[20];
    if (rtcTimeSet)
        snprintf_P(ts, sizeof(ts), PSTR("%04u-%02u-%02u %02u:%02u:%02u"),
                   (unsigned)rtcYear, (unsigned)rtcMonth,  (unsigned)rtcDay,
                   (unsigned)rtcHour, (unsigned)rtcMinute, (unsigned)rtcSecond);
    else
        strcpy_P(ts, PSTR("0000-00-00 00:00:00"));

    int n = snprintf_P(jbuf, sizeof(jbuf), PSTR(
                "{\"node\":\"%s\",\"voltage\":%s,\"current\":%s,\"power\":%s,"
                "\"pf\":%s,\"balance\":%s,\"rate\":%s,\"last_id\":\"%lu\","
                "\"datetime\":\"%s\"}"),
                NODE_ID, v, i, p, f, b, r, (unsigned long)lastTxId, ts);

    if (n < 0) n = 0;
    if (n > (int)sizeof(jbuf) - 1) n = (int)sizeof(jbuf) - 1;   // truncated
    return (uint16_t)n;
}

void esp01_init() {
#if !ESP_ON_HW_SERIAL
    ESP.begin(ESP_BAUD);
#endif
    espWait(3000);

    espCmd(F("AT"),         ESP_T_ATCMD);
    espCmd(F("AT+CWMODE=1"), ESP_T_ATCMD);

#if ENABLE_DEBUG && ESP_ECHO
    Serial.println(F("\n> AT+CWJAP"));
#endif
    ESP.print(F("AT+CWJAP=\""));
    ESP.print(F(WIFI_SSID));
    ESP.print(F("\",\""));
    ESP.print(F(WIFI_PASS));
    ESP.println(F("\""));
    espWait(ESP_T_JOIN);

    espCmd(F("AT+CIFSR"),    1500);
    espCmd(F("AT+CIPMUX=0"), ESP_T_ATCMD);

    DBGLN(F("[ESP01] ready"));
}

bool esp01_sendHttpTelemetry() {
    if (gaspPending) return false;

    uint16_t jsonLen = buildJson();
    uint8_t  digits  = (jsonLen < 10) ? 1 : (jsonLen < 100 ? 2 : 3);
    uint16_t totalLen = (uint16_t)(HDR_FIXED_LEN + digits + jsonLen);

    ESP.print(F("AT+CIPSTART=\"TCP\",\""));
    ESP.print(F(HTTP_HOST));
    ESP.print(F("\","));
    ESP.println(HTTP_PORT);
    if (!espWait(ESP_T_CIPSTART)) return false;

    ESP.print(F("AT+CIPSEND="));
    ESP.println(totalLen);
    if (!espWait(ESP_T_CIPSEND)) return false;

    // Streamed straight out. Only jbuf lives in RAM.
    ESP.print(FS(H1));  ESP.print(F(HTTP_PATH));
    ESP.print(FS(H2));  ESP.print(F(HTTP_HOST));
    ESP.print(FS(H3));  ESP.print(jsonLen);
    ESP.print(FS(H4));
    ESP.print(jbuf);

    if (!espWait(ESP_T_BODY)) return false;

    espCmd(F("AT+CIPCLOSE"), ESP_T_CIPCLOSE);

    DBG(F("[HTTP] posted "));
    DBGLN(jsonLen);
    return true;
}