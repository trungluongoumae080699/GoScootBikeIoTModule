#pragma once

#include <Arduino.h>
#include <SoftwareSerial.h>


struct QrScannerUtilityNonBlocking
{
    SoftwareSerial &serial;

    // Internal buffers
    String buffer;    // accumulating current line
    String lastCode;  // last completed QR code
    bool   hasNewCode = false;

    // Settings
    size_t maxLength      = 512;     // safety guard for crazy-long QR
    char   lineTerminator = '\n';    // we treat CR/LF as end of code

    explicit QrScannerUtilityNonBlocking(SoftwareSerial &s)
        : serial(s)
    {}

    // -------------------------------------------------
    // Init
    // -------------------------------------------------
    void begin(uint32_t baud = 9600)
    {
        serial.begin(baud);
        buffer.reserve(128);
    }

    void setMaxLength(size_t len)
    {
        maxLength = len;
    }

    // -------------------------------------------------
    // Non-blocking step – call in loop()
    // -------------------------------------------------
    void step()
    {
        while (serial.available())
        {
            char c = serial.read();

            // Treat CR or LF as end of code
            if (c == '\r' || c == '\n')
            {
                buffer.trim();
                if (buffer.length() > 0)
                {
                    lastCode   = buffer;
                    hasNewCode = true;

                    Serial.print(F("[QR] code received: "));
                    Serial.println(lastCode);
                }
                buffer = "";
            }
            else
            {
                buffer += c;
                if (buffer.length() > maxLength)
                {
                    // Safety: discard insane payload
                    buffer = "";
                }
            }
        }
    }

    // -------------------------------------------------
    // Result access
    // -------------------------------------------------
    bool isScanReady() const
    {
        return hasNewCode;
    }

    String takeResult()
    {
        String out = lastCode;
        hasNewCode = false;
        return out;
    }

    void reset()
    {
        buffer     = "";
        lastCode   = "";
        hasNewCode = false;
    }
};