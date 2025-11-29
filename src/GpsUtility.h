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
        return gps.satellites.value() >= 4;
    }
};