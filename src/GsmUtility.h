#define TINY_GSM_MODEM_SIM7600
#pragma once

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <time.h>
#include "Telemetry.h"

// IMPORTANT: In main.cpp you must:
// #define TINY_GSM_MODEM_SIM7600
// BEFORE including TinyGsmClient.h / this header.

struct GsmUtility
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
    TinyGsmClient netClient;
    PubSubClient mqtt;

    // Optional publish interval tracking
    unsigned long lastPublish = 0;
    unsigned long publishIntervalMs = 5000;

    // Constructor
    GsmUtility(
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

    // ---------------- Modem / network setup ----------------
    void setupModem()
    {
        Serial.println(F("Starting modem..."));
        serialAT.begin(115200);
        delay(3000); // allow modem to boot

        Serial.println(F("Restarting modem"));
        if (!modem.restart())
        {
            Serial.println(F("Modem restart failed"));
        }

        Serial.print(F("Modem info: "));
        Serial.println(modem.getModemInfo());

        Serial.print(F("Waiting for network..."));
        if (!modem.waitForNetwork())
        {
            Serial.println(F(" fail"));
            return;
        }
        Serial.println(F(" OK"));

        Serial.print(F("Connecting to APN..."));
        if (!modem.gprsConnect(apn, gprsUser, gprsPass))
        {
            Serial.println(F(" fail"));
            return;
        }
        Serial.println(F(" OK, GPRS up"));

        mqtt.setServer(mqttHost, mqttPort);
    }

    // ---------------- Keep MQTT connected ----------------
    bool ensureMqttConnected(const char *clientIdPrefix, const char *subscribeTopic = nullptr)
    {
        if (mqtt.connected())
            return true;

        Serial.print(F("Connecting to MQTT..."));
        String clientId = String(clientIdPrefix) + String(random(0xffff), HEX);

        if (mqtt.connect(clientId.c_str(), mqttUser, mqttPass))
        {
            Serial.println(F(" connected"));
            if (subscribeTopic)
            {
                mqtt.subscribe(subscribeTopic);
            }
            return true;
        }
        else
        {
            Serial.print(F(" failed, rc="));
            Serial.println(mqtt.state());
            return false;
        }
    }

    // Call this every loop to process incoming MQTT
    void loop()
    {
        mqtt.loop();
    }

    // ---------------- Time from modem ----------------
 int64_t getUnixTimestamp()
{
    modem.sendAT("+CCLK?");

    // 1) First line: echo "AT+CCLK?"
    String line1 = modem.stream.readStringUntil('\n');
    line1.trim();
    // Optional debug:
    // Serial.print("Line1: ");
    // Serial.println(line1);

    // 2) Second line: should be +CCLK: "24/11/28,07:35:44+08"
    String res = modem.stream.readStringUntil('\n');
    res.trim();
    Serial.print("CCLK raw: ");
    Serial.println(res);

    if (!res.startsWith("+CCLK"))
    {
        Serial.println("CCLK not found");
        return -1;
    }

    // Example: +CCLK: "24/11/28,07:35:44+08"
    int y  = res.substring(8, 10).toInt() + 2000;
    int mo = res.substring(11, 13).toInt();
    int d  = res.substring(14, 16).toInt();
    int h  = res.substring(17, 19).toInt();
    int mi = res.substring(20, 22).toInt();
    int s  = res.substring(23, 25).toInt();

    tm timeinfo{};
    timeinfo.tm_year = y - 1900;
    timeinfo.tm_mon  = mo - 1;
    timeinfo.tm_mday = d;
    timeinfo.tm_hour = h;
    timeinfo.tm_min  = mi;
    timeinfo.tm_sec  = s;

    time_t t = mktime(&timeinfo); // seconds since 1970
    Serial.print("Unix seconds: ");
    Serial.println((long)t);

    return (int64_t)t * 1000LL;   // ms
}

    // ---------------- Publish Telemetry ----------------
    bool publishTelemetry(const Telemetry &t, const char *topic)
    {
        uint8_t buffer[256];
        int size = encodeTelemetry(t, buffer);

        Serial.print(F("Publishing telemetry, bytes="));
        Serial.println(size);

        bool ok = mqtt.publish(topic, buffer, size);
        if (!ok)
        {
            Serial.println(F("MQTT publish failed"));
        }
        return ok;
    }
};