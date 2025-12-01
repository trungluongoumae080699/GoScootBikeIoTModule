#define TINY_GSM_MODEM_SIM7600 // MUST be first, before any TinyGSM include

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

Bike currentBike;

// ----------------- GSM / Network config -----------------
const char APN[] = "v-internet"; // e.g. "v-internet"
const char GPRS_USER[] = "";
const char GPRS_PASS[] = "";

// ----------------- MQTT config -----------------
const char *MQTT_HOST = "0.tcp.ap.ngrok.io";
const uint16_t MQTT_PORT = 12123;
const char *MQTT_USER = "BIK_298A1J35";
const char *MQTT_PASS = "TrungLuong080699!!!";
const char *MQTT_TOPIC = "/telemetry/BIK_298A1J35";

// ----------------- Utilities -----------------
GpsUtility gpsUtil(&Serial1, 9600);
TinyGPSPlus gps;

GsmUtility gsm(
    Serial2,
    APN, GPRS_USER, GPRS_PASS,
    MQTT_HOST, MQTT_PORT,
    MQTT_USER, MQTT_PASS);

// Battery sensor
int sensorPin = A0; // OUT from voltage sensor
float RATIO = 5.0;  // divider ratio
boolean isInside = false;
float last_gps_long = 0;
float last_gps_lat = 0;
int64_t last_gps_contact_time = 0;

// ----------------- Helpers -----------------
float readBatteryVoltage()
{
    int raw = analogRead(sensorPin);
    float voltage = (raw * 5.0f / 1023.0f); // 0–5V on A0
    return voltage * RATIO;                 // back to real battery voltage
}

int batteryPercentage(float v)
{
    if (v >= 8.4)
        return 100;
    if (v >= 8.2)
        return 90;
    if (v >= 8.0)
        return 80;
    if (v >= 7.8)
        return 70;
    if (v >= 7.6)
        return 60;
    if (v >= 7.4)
        return 50;
    if (v >= 7.2)
        return 40;
    if (v >= 7.0)
        return 30;
    if (v >= 6.8)
        return 20;
    if (v >= 6.6)
        return 10;
    return 0;
}

String generateUUID()
{
    const char *alphabet =
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    String uuid = "";
    int sections[] = {8, 4, 4, 4, 12};

    for (int s = 0; s < 5; s++)
    {
        if (s > 0)
            uuid += "-";

        int len = sections[s];
        for (int i = 0; i < len; i++)
        {
            int r = random(0, 62);
            uuid += alphabet[r];
        }
    }
    return uuid;
}

void setup()
{
    Serial.begin(115200);
    randomSeed(analogRead(A0));
    currentBike.userName = "BIK_298A1J35";
    currentBike.password = "TrungLuong080699!!!";
    currentBike.battery = 100;
    // GPS
    gpsUtil.begin();
    // GSM / MQTT
    gsm.setupModem();
}

void loop()
{

    // Keep MQTT alive
    if (!gsm.ensureMqttConnected("goscoot-bike-", "goscoot/control/bike001"))
    {
        delay(2000);
        return;
    }
    gsm.loop();

    // ---- GPS ----
    gpsUtil.update();
    float lat = 0, lng = 0;

    if (gpsUtil.getLocation(lat, lng))
    {
        isInside = true;
        last_gps_contact_time = gsm.getUnixTimestamp();
        last_gps_lat = lat;
        last_gps_long = lng;
    }
    else
    {
        Serial.println("Waiting for GPS fix...");
        isInside = false;
        String cellJson = gsm.getCellTowerJSON();
        if (cellJson.length() > 0)
        {
            String resp = gsm.httpPostJson("http://eu1.unwiredlabs.com/v2/process.php", cellJson);
            Serial.println("Location API response:");
            Serial.println(resp);
        }
    }

    // ---- Battery ----
    int percent = 100;

    currentBike.longitude = lat;
    currentBike.latitude = lng;
    currentBike.battery = percent;

    Telemetry t;
    t.id = generateUUID();
    t.bikeId = currentBike.userName;
    t.longitude = lng;
    t.latitude = lat;
    t.battery = percent;
    t.time = gsm.getUnixTimestamp();
    t.last_gps_contact_time = last_gps_contact_time;
    t.last_gps_lat = last_gps_lat;
    t.last_gps_long = last_gps_long;

    gsm.publishTelemetry(t, MQTT_TOPIC);

    delay(5000);
}