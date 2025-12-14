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
        delay(3000); // allow modem to boot

        Serial.println(F("[GSM] Restarting modem"));
        if (!modem.restart())
        {
            Serial.println(F("[GSM] Modem restart failed"));
            return false;
        }

        Serial.print(F("[GSM] Modem info: "));
        //Serial.println(modem.getModemInfo());

        Serial.print(F("[GSM] Waiting for network..."));
        if (!modem.waitForNetwork())
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
};