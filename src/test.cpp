#include <Arduino.h>

#include <TinyGPSPlus.h>

TinyGPSPlus gps;

// Change this if your GPS is not 9600.
// From your output, you already found the correct baud.
static const uint32_t GPS_BAUD = 38400;

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  Serial.println("=== Serial1 GPS Parse Test (TinyGPS++) ===");
  Serial1.begin(GPS_BAUD);
}

void loop() {
  // Feed TinyGPS++ with Serial1 bytes
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    gps.encode(c);

    // Optional: raw monitor (comment out if too noisy)
    // Serial.write(c);
  }

  // Print every 1s when we get updated location
  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();

    Serial.print("chars=");
    Serial.print(gps.charsProcessed());
    Serial.print("  sentences=");
    Serial.print(gps.sentencesWithFix());
    Serial.print("  failedChecksum=");
    Serial.print(gps.failedChecksum());
    Serial.println();

    if (gps.location.isValid()) {
      Serial.print("LAT=");
      Serial.print(gps.location.lat(), 6);
      Serial.print("  LNG=");
      Serial.print(gps.location.lng(), 6);
      Serial.print("  age(ms)=");
      Serial.print(gps.location.age());
      Serial.println();
    } else {
      Serial.println("Location: INVALID (no fix yet)");
    }

    Serial.print("Sats=");
    Serial.print(gps.satellites.isValid() ? gps.satellites.value() : 0);
    Serial.print("  HDOP=");
    Serial.print(gps.hdop.isValid() ? gps.hdop.hdop() : 0.0, 2);
    Serial.println();

    Serial.println("---------------------------");
  }
}