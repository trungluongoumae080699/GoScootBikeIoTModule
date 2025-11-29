#define TINY_GSM_MODEM_SIM7600   // MUST be first, before any TinyGSM include

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <time.h>

#include "Bike.h"
#include "Telemetry.h"
#include "GpsUtility.h"
#include "GsmUtility.h"

// ----------------- GSM / Network config -----------------
const char APN[]       = "your_apn_here";         // e.g. "v-internet"
const char GPRS_USER[] = "";
const char GPRS_PASS[] = "";

// ----------------- MQTT config -----------------
const char* MQTT_HOST  = "your-mqtt-broker.com";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER  = "mqtt_username";         // optional
const char* MQTT_PASS  = "mqtt_password";         // optional
const char* MQTT_TOPIC = "goscoot/telemetry/bike001";

// ----------------- Utilities -----------------
GpsUtility gpsUtil(&Serial1, 9600);

GsmUtility gsm(
    Serial2,
    APN, GPRS_USER, GPRS_PASS,
    MQTT_HOST, MQTT_PORT,
    MQTT_USER, MQTT_PASS
);

Bike currentBike;

// Battery sensor
int   sensorPin = A0;   // OUT from voltage sensor
float RATIO     = 5.0;  // divider ratio

// ----------------- Helpers -----------------
float readBatteryVoltage() {
    int raw = analogRead(sensorPin);
    float voltage = (raw * 5.0f / 1023.0f); // 0–5V on A0
    return voltage * RATIO;                 // back to real battery voltage
}

int batteryPercentage(float v) {
    if (v >= 8.4) return 100;
    if (v >= 8.2) return 90;
    if (v >= 8.0) return 80;
    if (v >= 7.8) return 70;
    if (v >= 7.6) return 60;
    if (v >= 7.4) return 50;
    if (v >= 7.2) return 40;
    if (v >= 7.0) return 30;
    if (v >= 6.8) return 20;
    if (v >= 6.6) return 10;
    return 0;
}

String generateUUID() {
    const char* alphabet =
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    String uuid = "";
    int sections[] = {8, 4, 4, 4, 12};

    for (int s = 0; s < 5; s++) {
        if (s > 0) uuid += "-";

        int len = sections[s];
        for (int i = 0; i < len; i++) {
            int r = random(0, 62);
            uuid += alphabet[r];
        }
    }
    return uuid;
}

void setup() {
    Serial.begin(115200);
    randomSeed(analogRead(A0));
    currentBike.userName = "BIK_298A1J34";
    currentBike.password = "";
    currentBike.battery  = 100;
    // GPS
    gpsUtil.begin();
    // GSM / MQTT
    gsm.setupModem();
}

void loop() {
    // Keep MQTT alive
    if (!gsm.ensureMqttConnected("goscoot-bike-", "goscoot/control/bike001")) {
        delay(2000);
        return;
    }
    gsm.loop();

    // ---- GPS ----
    gpsUtil.update();
    float lat = 0, lng = 0;

    if (gpsUtil.getLocation(lat, lng)) {
        Serial.print("LAT = ");
        Serial.println(lat, 6);
        Serial.print("LNG = ");
        Serial.println(lng, 6);
    } else {
        Serial.println("Waiting for GPS fix...");
    }

    // ---- Battery ----
    float vBat   = readBatteryVoltage();
    int   percent = batteryPercentage(vBat);

    currentBike.longitude = lat;
    currentBike.latitude  = lng;
    currentBike.battery   = percent;

    Telemetry t;
    t.id       = generateUUID();
    t.bikeId   = currentBike.userName;
    t.longitude = lng;
    t.latitude  = lat;
    t.battery   = percent;
    t.time      = gsm.getUnixTimestamp();   

    gsm.publishTelemetry(t, MQTT_TOPIC);

    delay(1000);
}