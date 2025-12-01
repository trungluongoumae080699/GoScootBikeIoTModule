#define TINY_GSM_MODEM_SIM7600
#pragma once

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <time.h>
#include "Telemetry.h"
#include <CellInfo.h>

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
    TinyGsmClient netClient; // for MQTT
    // TinyGsmClient httpClient;  // for HTTP
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
          // httpClient(modem),
          mqtt(netClient)
    {
    }

    String readLineFromModem(uint32_t timeoutMs = 1000)
    {
        uint32_t start = millis();
        while (millis() - start < timeoutMs)
        {
            if (modem.stream.available())
            {
                String line = modem.stream.readStringUntil('\n');
                line.trim();
                return line;
            }
        }
        return "";
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

        uint32_t start = millis();
        String line;
        String cclkLine;

        Serial.println(F("---- Reading CCLK response ----"));

        while (millis() - start < 2000)
        {
            if (modem.stream.available())
            {
                line = modem.stream.readStringUntil('\n');
                line.trim();
                if (line.length() == 0)
                    continue;

                Serial.print(F("LINE: "));
                Serial.println(line);

                if (line.startsWith("+CCLK"))
                {
                    cclkLine = line;
                }
                if (line == "OK")
                    break;
                if (line.indexOf("ERROR") >= 0)
                {
                    Serial.println(F("CCLK returned ERROR"));
                    return -1;
                }
            }
        }

        if (cclkLine.length() == 0)
        {
            Serial.println(F("CCLK not found within timeout"));
            return -1;
        }

        int quote1 = cclkLine.indexOf('"');
        int quote2 = cclkLine.indexOf('"', quote1 + 1);
        if (quote1 < 0 || quote2 < 0)
        {
            Serial.println(F("CCLK line format invalid"));
            return -1;
        }

        String datetime = cclkLine.substring(quote1 + 1, quote2);
        int y = datetime.substring(0, 2).toInt() + 2000;
        int mo = datetime.substring(3, 5).toInt();
        int d = datetime.substring(6, 8).toInt();
        int h = datetime.substring(9, 11).toInt();
        int mi = datetime.substring(12, 14).toInt();
        int s = datetime.substring(15, 17).toInt();

        tm timeinfo{};
        timeinfo.tm_year = y - 1900;
        timeinfo.tm_mon = mo - 1;
        timeinfo.tm_mday = d;
        timeinfo.tm_hour = h;
        timeinfo.tm_min = mi;
        timeinfo.tm_sec = s;

        time_t t = mktime(&timeinfo);
        Serial.print(F("Unix seconds: "));
        Serial.println((long)t);

        return (int64_t)t * 1000LL;
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

    // ---------- small helper for parsing ints ----------
    int safeInt(const String &s)
    {
        if (s.length() == 0)
            return -1;
        return s.toInt();
    }

    // ---------- read all pending data from modem for some time ----------
    String readAllFromModem(uint16_t timeoutMs = 500)
    {
        String res;
        unsigned long start = millis();
        while (millis() - start < timeoutMs)
        {
            while (modem.stream.available())
            {
                char c = modem.stream.read();
                res += c;
            }
        }
        return res;
    }

    // ---------------- Cellular tower JSON (for indoor LBS) ----------------
    String getCellTowerJSON()
    {
        // Clear any leftover bytes
        while (modem.stream.available())
        {
            modem.stream.read();
        }

        modem.sendAT("+CPSI?");

        Serial.println(F("---- CPSI RESPONSE ----"));

        String cpsiLine;
        uint32_t start = millis();
        while (millis() - start < 2000)
        { // read for up to 2 seconds
            String line = readLineFromModem(500);
            if (line.length() == 0)
                continue;

            Serial.print(F("LINE: "));
            Serial.println(line);

            if (line.startsWith("+CPSI"))
            {
                cpsiLine = line;
            }
            if (line == "OK")
            {
                break; // end of response
            }
            if (line.indexOf("ERROR") >= 0)
            {
                Serial.println(F("CPSI returned ERROR"));
                break;
            }
        }

        if (cpsiLine.length() == 0)
        {
            Serial.println(F("No +CPSI line found"));
            return String(); // empty = failure
        }

        Serial.print(F("Using CPSI line: "));
        Serial.println(cpsiLine);

        CellInfo cell;
        if (!cell.parseCpsiLine(cpsiLine))
        {
            Serial.println(F("Failed to parse CPSI line"));
            return String();
        }

        String json = cell.buildLocationApiJson();
        Serial.println(F("LocationAPI JSON:"));
        Serial.println(json);

        return json;
    }

    // --------------------------------------------------
    //            HTTP HELPERS (part of GsmUtility)
    // --------------------------------------------------

    // full-url style: "http://example.com:8080/path"
    String httpGet(const char *url)
    {
        const char *host = urlHost(url);
        int port = urlPort(url);
        const char *path = urlPath(url);

        Serial.print(F("[HTTP] GET "));
        Serial.print(host);
        Serial.print(":");
        Serial.print(port);
        Serial.println(path);

        // IMPORTANT: don't keep MQTT using netClient at the same time
        mqtt.disconnect(); // optional but safer

        if (!netClient.connect(host, port))
        {
            Serial.println(F("[HTTP] GET connect failed"));
            return "";
        }

        netClient.print(String("GET ") + path + " HTTP/1.1\r\n");
        netClient.print(String("Host: ") + host + "\r\n");
        netClient.print("Connection: close\r\n");
        netClient.print("\r\n");

        String resp = readHttpResponse();
        return resp;
    }

    String httpPostJson(const char *url, const String &jsonBody)
    {
        const char *host = urlHost(url);
        int port = urlPort(url);
        const char *path = urlPath(url);

        Serial.print(F("[HTTP] POST "));
        Serial.print(host);
        Serial.print(":");
        Serial.print(port);
        Serial.println(path);

        mqtt.disconnect(); // optional but safer

        if (!netClient.connect(host, port))
        {
            Serial.println(F("[HTTP] POST connect failed"));
            return "";
        }

        netClient.print(String("POST ") + path + " HTTP/1.1\r\n");
        netClient.print(String("Host: ") + host + "\r\n");
        netClient.print("Content-Type: application/json\r\n");
        netClient.print(String("Content-Length: ") + jsonBody.length() + "\r\n");
        netClient.print("Connection: close\r\n\r\n");
        netClient.print(jsonBody);

        String resp = readHttpResponse();
        return resp;
    }

    // Use netClient here too
    String readHttpResponse()
    {
        String resp;
        uint32_t start = millis();

        while (millis() - start < 10000)
        {
            while (netClient.available())
            {
                char c = netClient.read();
                resp += c;
            }
            if (!netClient.connected())
                break;
        }

        netClient.stop();
        return resp;
    }

    // ---- URL parsing helpers ----
    const char *urlHost(const char *url)
    {
        const char *p = strstr(url, "://");
        if (p)
            url = p + 3;

        static char host[100];
        int i = 0;
        while (*url && *url != '/' && *url != ':' && i < 99)
        {
            host[i++] = *url++;
        }
        host[i] = 0;
        return host;
    }

    int urlPort(const char *url)
    {
        const char *p = strstr(url, "://");
        if (p)
            url = p + 3;

        while (*url && *url != '/' && *url != ':')
            url++;

        if (*url == ':')
            return atoi(url + 1);
        return 80;
    }

    const char *urlPath(const char *url)
    {
        const char *p = strstr(url, "://");
        if (p)
            url = p + 3;

        while (*url && *url != '/')
            url++;

        return (*url ? url : "/");
    }
};