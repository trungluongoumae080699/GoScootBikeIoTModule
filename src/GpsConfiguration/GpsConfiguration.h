#pragma once
#include <TinyGPSPlus.h>

struct GpsConfiguration
{
    TinyGPSPlus gps;
    HardwareSerial *serial = nullptr;

    // Constructor – chỉ nhận Serial, không nhận baud nữa
    GpsConfiguration(HardwareSerial *s = nullptr)
        : serial(s) {}

    // Always use 38400 for NEO-M10
    void begin() {
        if (serial) serial->begin(38400);
    }

    void update() {
        if (!serial) return;

        while (serial->available() > 0) {
            gps.encode(serial->read());
        }
    }

    bool getLocation(float &lat, float &lng) {
        Serial.println("Getting location from GPS...");

        if (gps.location.isValid()) {
            lat = gps.location.lat();
            lng = gps.location.lng();
            Serial.print("FIX: ");
            Serial.print(lat, 6);
            Serial.print(", ");
            Serial.println(lng, 6);
            return true;
        }

        Serial.println("No valid GPS location.");
        return false;
    }

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

    bool hasFix() {
        return gps.location.isValid();
    }
};