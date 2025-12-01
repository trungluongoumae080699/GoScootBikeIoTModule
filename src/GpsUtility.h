#pragma once
#include <TinyGPSPlus.h>

struct GpsUtility
{
    TinyGPSPlus gps;
    HardwareSerial *serial = nullptr;
    uint32_t baudRate = 9600;

    GpsUtility(HardwareSerial *s = nullptr, uint32_t baud = 9600)
        : serial(s), baudRate(baud) {}

    void begin() {
        if (serial) serial->begin(baudRate);
    }

    void update() {
        if (!serial) return;
        while (serial->available() > 0) {
            gps.encode(serial->read());
        }
    }

    bool getLocation(float &lat, float &lng) {
        if (gps.location.isValid() && gps.location.age() < 2000) {
            lat = gps.location.lat();
            lng = gps.location.lng();
            return true;
        }
        return false;
    }

    bool hasFix() {
        return gps.satellites.isValid() && gps.satellites.value() >= 4;
    }

    // -----------------------------
    // 🔥 NEW: Debug printer function
    // -----------------------------
    void printDebug() {
        Serial.print("Satellites: ");
        if (gps.satellites.isValid())
            Serial.print(gps.satellites.value());
        else
            Serial.print("invalid");

        Serial.print(" | Location: ");
        if (gps.location.isValid()) {
            Serial.print(gps.location.lat(), 6);
            Serial.print(", ");
            Serial.print(gps.location.lng(), 6);
        } else {
            Serial.print("NO FIX");
        }

        Serial.print(" | Chars: ");
        Serial.print(gps.charsProcessed());

        Serial.print(" | Fix sentences: ");
        Serial.print(gps.sentencesWithFix());

        Serial.print(" | Age: ");
        Serial.print(gps.location.age());

        Serial.println();
    }
};