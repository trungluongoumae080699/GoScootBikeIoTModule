#define TINY_GSM_MODEM_SIM7600
#pragma once
#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>

struct HttpConfiguration
{
    TinyGsmClient &netClient;
    PubSubClient *mqtt;            // optional, có thể nullptr
    unsigned long httpTimeoutMs = 10000;  // default 10s (can be set from GsmConfiguration)
    explicit HttpConfiguration(TinyGsmClient &client, PubSubClient *mqttClient = nullptr)
        : netClient(client), mqtt(mqttClient) {}

    // ---------------- URL helpers ----------------

    static const char *urlHost(const char *url)
    {
        const char *p = strstr(url, "://");
        if (p)
            url = p + 3; // skip "http://"

        static char hostBuf[100];
        int i = 0;
        while (*url && *url != '/' && *url != ':' && i < 99)
        {
            hostBuf[i++] = *url++;
        }
        hostBuf[i] = 0;
        return hostBuf;
    }

    static int urlPort(const char *url)
    {
        const char *p = strstr(url, "://");
        if (p)
            url = p + 3;

        while (*url && *url != '/' && *url != ':')
            ++url;

        if (*url == ':')
            return atoi(url + 1);
        return 80; // default HTTP
    }

    static const char *urlPath(const char *url)
    {
        const char *p = strstr(url, "://");
        if (p)
            url = p + 3;

        while (*url && *url != '/')
            ++url;

        return (*url ? url : "/");
    }

    // ===================================================
    //  Non-blocking HTTP state
    // ===================================================

    enum HttpState
    {
        HTTP_IDLE,
        HTTP_READING,
        HTTP_DONE,
        HTTP_ERROR
    };

    HttpState      httpState       = HTTP_IDLE;
    unsigned long  httpStartMs     = 0;
    unsigned long  httpEffectiveMs = 0;   // timeout used for current request
    String         httpResponseBuf;
    String         httpHost;
    int            httpPort        = 80;
    String         httpPath;
    String         httpBody;
    bool           httpIsPost      = false;

    // ===================================================
    //  Start HTTP POST JSON (non-blocking)
    //  - Returns false if already busy
    //  - timeoutOverrideMs == 0 => use httpTimeoutMs
    // ===================================================

    bool startHttpPostJson(const char *url, const String &jsonBody, uint32_t timeoutOverrideMs = 0)
    {
        if (httpState != HTTP_IDLE)
        {
            Serial.println(F("[HTTP] Busy, cannot start new POST"));
            return false;
        }

        if (mqtt)
        {
            mqtt->disconnect(); // shared client: stop MQTT before HTTP
        }

        httpHost = urlHost(url);
        httpPort = urlPort(url);
        httpPath = urlPath(url);
        httpBody = jsonBody;
        httpIsPost = true;

        httpResponseBuf = "";
        httpEffectiveMs = (timeoutOverrideMs > 0) ? timeoutOverrideMs : httpTimeoutMs;

        Serial.print(F("[HTTP] POST "));
        Serial.print(httpHost);
        Serial.print(F(":"));
        Serial.print(httpPort);
        Serial.println(httpPath);

        if (!netClient.connect(httpHost.c_str(), httpPort))
        {
            Serial.println(F("[HTTP] connect failed"));
            httpState = HTTP_ERROR;
            return false;
        }

        netClient.print(String("POST ") + httpPath + " HTTP/1.1\r\n");
        netClient.print(String("Host: ") + httpHost + "\r\n");
        netClient.print("Content-Type: application/json\r\n");
        netClient.print(String("Content-Length: ") + httpBody.length() + "\r\n");
        netClient.print("Connection: close\r\n\r\n");
        netClient.print(httpBody);

        httpStartMs = millis();
        httpState   = HTTP_READING;

        return true;
    }

    // (Optional) non-blocking GET with same pattern
    bool startHttpGet(const char *url, uint32_t timeoutOverrideMs = 0)
    {
        if (httpState != HTTP_IDLE)
        {
            Serial.println(F("[HTTP] Busy, cannot start new GET"));
            return false;
        }

        if (mqtt)
        {
            mqtt->disconnect();
        }

        httpHost = urlHost(url);
        httpPort = urlPort(url);
        httpPath = urlPath(url);
        httpBody = "";
        httpIsPost = false;

        httpResponseBuf = "";
        httpEffectiveMs = (timeoutOverrideMs > 0) ? timeoutOverrideMs : httpTimeoutMs;

        Serial.print(F("[HTTP] GET "));
        Serial.print(httpHost);
        Serial.print(F(":"));
        Serial.print(httpPort);
        Serial.println(httpPath);

        if (!netClient.connect(httpHost.c_str(), httpPort))
        {
            Serial.println(F("[HTTP] connect failed"));
            httpState = HTTP_ERROR;
            return false;
        }

        netClient.print(String("GET ") + httpPath + " HTTP/1.1\r\n");
        netClient.print(String("Host: ") + httpHost + "\r\n");
        netClient.print("Connection: close\r\n\r\n");

        httpStartMs = millis();
        httpState   = HTTP_READING;

        return true;
    }

    // ===================================================
    //  Step HTTP (non-blocking)
    //  - Call this every loop()
    //  - Handles timeout and closing TCP socket
    // ===================================================

    void stepHttp()
    {
        if (httpState != HTTP_READING)
            return;

        // read all available bytes
        while (netClient.available())
        {
            char c = netClient.read();
            httpResponseBuf += c;

            // reset timeout each time we get data
            httpStartMs = millis();
        }

        // check timeout or disconnected
        if (!netClient.connected() || (millis() - httpStartMs > httpEffectiveMs))
        {
            netClient.stop();  // ⛔ hard cut: modem stops listening to this TCP session

            if (httpResponseBuf.length() == 0)
            {
                Serial.println(F("[HTTP] empty response or timeout"));
                httpState = HTTP_ERROR;
            }
            else
            {
                httpState = HTTP_DONE;
            }
        }
    }

    // ===================================================
    //  Query helpers
    // ===================================================

    bool isIdle() const
    {
        return httpState == HTTP_IDLE;
    }

    bool isHttpDone() const
    {
        return httpState == HTTP_DONE || httpState == HTTP_ERROR;
    }

    bool isHttpOk() const
    {
        return httpState == HTTP_DONE;
    }

    String getHttpResult() const
    {
        return httpResponseBuf;
    }

    void resetHttp()
    {
        httpState       = HTTP_IDLE;
        httpResponseBuf = "";
        httpHost        = "";
        httpPath        = "";
        httpBody        = "";
        httpIsPost      = false;
        httpPort        = 80;
        httpEffectiveMs = 0;
    }
};