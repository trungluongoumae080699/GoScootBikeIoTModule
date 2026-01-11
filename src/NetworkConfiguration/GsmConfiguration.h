#define TINY_GSM_MODEM_SIM7600
#pragma once

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <time.h>
#include "Domains/Telemetry.h"
#include "Domains/CellInfo.h"

struct GsmConfiguration
{
    // --- Config ---
    HardwareSerial &serialAT;
    const char *apn;
    const char *gprsUser;
    const char *gprsPass;

    const char *mqttHost;
    uint16_t mqttPort;
    const char *mqttUser;
    const char *mqttPass;

    // --- GSM + MQTT objects ---
    TinyGsm modem;
    TinyGsmClient netClient; // shared for MQTT + HTTP
    PubSubClient mqtt;

    GsmConfiguration(
        HardwareSerial &serial,
        const char *apn,
        const char *gprsUser,
        const char *gprsPass,
        const char *mqttHost,
        uint16_t mqttPort,
        const char *mqttUser,
        const char *mqttPass)
        : serialAT(serial),
          apn(apn),
          gprsUser(gprsUser),
          gprsPass(gprsPass),
          mqttHost(mqttHost),
          mqttPort(mqttPort),
          mqttUser(mqttUser),
          mqttPass(mqttPass),
          modem(serial),
          netClient(modem),
          mqtt(netClient)
    {
    }

    // =====================================================
    // 1) MODEM / NETWORK SETUP (blocking, called in setup)
    // =====================================================
    bool setupModemBlocking(uint32_t baud = 115200)
    {
        Serial.println(F("[GSM] Starting modem..."));
        serialAT.begin(baud);
        delay(800);

        // Flush RX
        while (serialAT.available())
            serialAT.read();

        Serial.println(F("[GSM] AT handshake..."));
        bool atOk = false;
        for (int i = 0; i < 10; i++)
        {
            if (modem.testAT())
            {
                atOk = true;
                break;
            }
            delay(500);
        }
        if (!atOk)
        {
            Serial.println(F("[GSM] AT handshake FAILED"));
            return false;
        }

        // Echo off
        modem.sendAT("E0");
        modem.waitResponse(1000);

        Serial.print(F("[GSM] Waiting for network..."));
        if (!modem.waitForNetwork(60000L))
        {
            Serial.println(F(" fail"));
            return false;
        }
        Serial.println(F(" OK"));

        Serial.print(F("[GSM] Connecting to APN..."));
        if (!modem.gprsConnect(apn, gprsUser, gprsPass))
        {
            Serial.println(F(" fail"));
            return false;
        }
        Serial.println(F(" OK, GPRS up"));

        Serial.print(F("[GSM] localIP="));
        Serial.println(modem.localIP());

        mqtt.setServer(mqttHost, mqttPort);
        return true;
    }

    // =====================================================
    // 2) MQTT Keep-alive (very small “state machine”)
    // =====================================================

    // If you want, you can even drop these enums and just do a simple
    // “if not connected and retry interval passed -> connect” logic.

    unsigned long lastMqttAttemptMs = 0;
    unsigned long mqttRetryIntervalMs = 10000; // 10s

    void configureMqtt()
    {
    }

    void stepMqtt()
    {
        // keep-alive, non-blocking
        mqtt.loop();

        // If already connected, nothing to do
        if (mqtt.connected())
            return;

        unsigned long now = millis();
        if (now - lastMqttAttemptMs < mqttRetryIntervalMs)
            return; // not time to retry yet

        lastMqttAttemptMs = now;

        // Simple fixed prefix + random suffix client ID
        String clientId = String("goscoot-bike-") + String(random(0xffff), HEX);

        Serial.print(F("[MQTT] Connecting as "));
        Serial.println(clientId);

        bool ok = mqtt.connect(clientId.c_str(), mqttUser, mqttPass);
        if (!ok)
        {
            Serial.print(F("[MQTT] connect failed, rc="));
            Serial.println(mqtt.state());
            return;
        }

        Serial.println(F("[MQTT] connected"));

        // If later you need to subscribe to control topics:
        // mqtt.subscribe("goscoot/control/bike001");
    }

    bool mqttConnected()
    {
        return mqtt.connected();
    }

    // =====================================================
    // 3) Telemetry publish (fast, no internal state)
    // =====================================================

    bool publishMqtt(const uint8_t *data, size_t len, const char *topic)
    {
        if (!mqtt.connected())
        {
            Serial.println(F("[MQTT] publishBinary: not connected"));
            return false;
        }

        if (!data || len == 0)
        {
            Serial.println(F("[MQTT] publishBinary: invalid data"));
            return false;
        }

        bool ok = mqtt.publish(topic, data, len);

        if (!ok)
        {
            Serial.println(F("[MQTT] publishBinary FAILED"));
        }
        else
        {
            Serial.print(F("[MQTT] publishBinary OK, len="));
            Serial.println(len);
        }

        return ok;
    }

    /*
        bool setupModemBlocking(uint32_t baud = 115200)
{
    static bool serialInited = false;
    static uint8_t hardFailCount = 0;
    static uint32_t lastHardRestartMs = 0;

    // ---------- helpers ----------
    auto flushRx = [&](uint32_t ms = 200) {
        uint32_t t0 = millis();
        while (millis() - t0 < ms) {
            while (serialAT.available()) (void)serialAT.read();
            delay(1);
        }
    };

    auto isOkNumericOrText = [&](const String& s) -> bool {
        // text OK
        if (s.indexOf("OK") >= 0) return true;
        // numeric OK
        if (s.indexOf("\r\n0\r") >= 0) return true;
        if (s.endsWith("0\r")) return true;
        return false;
    };

    auto isErrNumericOrText = [&](const String& s) -> bool {
        if (s.indexOf("ERROR") >= 0) return true;
        // numeric ERROR often 4
        if (s.indexOf("\r\n4\r") >= 0) return true;
        if (s.endsWith("4\r")) return true;
        return false;
    };

    auto sendAT = [&](const char* cmd, uint32_t timeoutMs = 1500, uint32_t quietMs = 120) -> String {
        flushRx(80);

        serialAT.print(cmd);
        serialAT.print("\r\n");

        uint32_t start = millis();
        uint32_t lastRx = millis();
        bool gotAny = false;
        String out;

        while (millis() - start < timeoutMs) {
            while (serialAT.available()) {
                char c = (char)serialAT.read();
                if (c == '\0') continue; // NULL filter
                out += c;
                gotAny = true;
                lastRx = millis();
            }

            if (isOkNumericOrText(out) || isErrNumericOrText(out)) break;
            if (gotAny && (millis() - lastRx > quietMs)) break;
            delay(2);
        }
        return out;
    };

    auto atHandshake = [&]() -> bool {
        // Try a few times: modem may be busy right after power-up
        for (int i = 0; i < 6; i++) {
            String r = sendAT("AT", 1200);
            if (isOkNumericOrText(r)) return true;
            delay(250);
        }
        return false;
    };

    // ---------- init SerialAT ONCE ----------
    if (!serialInited) {
        Serial.println(F("[GSM] Starting modem... (SerialAT begin once)"));
        serialAT.begin(baud);
        delay(800);
        flushRx(400);
        serialInited = true;
    } else {
        Serial.println(F("[GSM] Starting modem... (re-try, no SerialAT re-begin)"));
    }

    // ---------- optional hard restart gate ----------
    // Only hard restart if we have failed many times AND cooldown passed.
    const uint8_t HARD_RESTART_AFTER = 6;
    const uint32_t HARD_RESTART_COOLDOWN_MS = 45000;

    if (hardFailCount >= HARD_RESTART_AFTER && (millis() - lastHardRestartMs) > HARD_RESTART_COOLDOWN_MS) {
        Serial.println(F("[GSM] Too many fails -> HARD restart modem (cooldown ok)"));
        lastHardRestartMs = millis();
        hardFailCount = 0;

        // You can keep this if your modem supports it reliably.
        // If it makes things worse, comment it out.
        modem.restart();
        delay(2500);
        flushRx(500);
    }

    // ---------- handshake ----------
    Serial.println(F("[GSM] AT handshake..."));
    if (!atHandshake()) {
        Serial.println(F("[GSM] AT handshake FAILED"));
        hardFailCount++;
        return false;
    }

    // echo off (ignore result)
    sendAT("ATE0", 1200);

    // SIM ready check (tolerant)
    Serial.println(F("[GSM] SIM check..."));
    {
        String r = sendAT("AT+CPIN?", 2500, 200);
        if (r.indexOf("+CPIN: READY") < 0) {
            Serial.println(F("[GSM] SIM not READY"));
            hardFailCount++;
            return false;
        }
    }

    // network reg check (CGREG is enough for data)
    Serial.print(F("[GSM] Waiting for network..."));
    bool regOK = false;
    for (int i = 0; i < 20; i++) {
        String r = sendAT("AT+CGREG?", 2000, 200);
        if (r.indexOf("+CGREG: 0,1") >= 0 || r.indexOf("+CGREG: 0,5") >= 0) {
            regOK = true;
            break;
        }
        Serial.print('.');
        delay(1000);
    }
    Serial.println(regOK ? F(" OK") : F(" FAIL"));
    if (!regOK) {
        hardFailCount++;
        return false;
    }

    // show CSQ
    {
        String r = sendAT("AT+CSQ", 2000, 200);
        Serial.print(F("[GSM] CSQ raw: "));
        Serial.println(r);
    }

    // Use TinyGSM gprsConnect IF you already patched TinyGSM numeric OK.
    // If not patched, skip gprsConnect and use raw SIMCom netopen path instead.
    Serial.print(F("[GSM] Connecting to APN..."));
    bool gprsOK = false;

    // ---- RAW data bring-up for SIMCom (numeric OK) ----
    // Set PDP context
    {
        String cmd = String("AT+CGDCONT=1,\"IP\",\"") + apn + "\"";
        sendAT(cmd.c_str(), 2500, 200);
    }

    // attach
    for (int i = 0; i < 8; i++) {
        String r = sendAT("AT+CGATT=1", 5000, 250);
        if (isOkNumericOrText(r)) { gprsOK = true; break; }
        delay(1200);
    }
    if (!gprsOK) {
        Serial.println(F(" fail (CGATT)"));
        hardFailCount++;
        return false;
    }

    // open network
    sendAT("AT+NETOPEN", 8000, 300);

    Serial.println(F(" OK (RAW data up)"));

    // IP
    {
        String r = sendAT("AT+CGPADDR=1", 2500, 250);
        Serial.print(F("[GSM] IP: "));
        Serial.println(r);
    }

    mqtt.setServer(mqttHost, mqttPort);

    hardFailCount = 0; // reset fail counter on success
    return true;
}
    */
};