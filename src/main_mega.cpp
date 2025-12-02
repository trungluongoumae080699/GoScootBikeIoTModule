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
GpsUtility gpsUtil(&Serial1);

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
    // 1) Luôn luôn đọc GPS + cho modem chạy
    gpsUtil.update();
    gsm.loop();

    unsigned long now = millis();

    // 2) Mỗi 1000 ms in debug + lấy tọa độ
    static unsigned long lastGpsPrint = 0;
    static float lastLat = 0, lastLng = 0;

    if (now - lastGpsPrint >= 1000)
    {
        lastGpsPrint = now;

        gpsUtil.printDebug();

        float lat, lng;
        if (gpsUtil.getLocation(lat, lng))
        {
            isInside = false;
            last_gps_contact_time = gsm.getUnixTimestamp();
            last_gps_lat = lat;
            last_gps_long = lng;
            lastLat = lat;
            lastLng = lng;
        }
        else
        {
            // Không có fix GPS -> đánh dấu "inside"
            isInside = true;
            
        }
    }

    // 3) Chỉ gọi cell-tower API 30s/lần khi không có GPS fix
    static unsigned long lastCellReq = 0;
    if (isInside && (now - lastCellReq >= 30000)) // 30 giây
    {
        lastCellReq = now;

        String cellJson = gsm.getCellTowerJSON();
        Serial.println("LocationAPI JSON:");
        Serial.println(cellJson);

        if (cellJson.length() > 0)
        {
            String resp = gsm.httpPostJson("http://eu1.unwiredlabs.com/v2/process.php", cellJson);
            Serial.println("Location API response:");
            Serial.println(resp);
        }
    }

    // 4) Đảm bảo MQTT (có thể 10s check 1 lần)
    static unsigned long lastMqttCheck = 0;
    if (now - lastMqttCheck >= 10000) // 10 giây
    {
        lastMqttCheck = now;
        if (!gsm.ensureMqttConnected("goscoot-bike-", "goscoot/control/bike001"))
        {
            // nếu fail thì thôi, lần sau check lại, KHÔNG delay lâu
            Serial.println("MQTT not connected, will retry...");
        }
    }

    // 5) Publish telemetry mỗi 5 giây
    static unsigned long lastTelemetry = 0;
    if (now - lastTelemetry >= 5000)
    {
        lastTelemetry = now;

        int percent = 100;
        currentBike.longitude = lastLat;
        currentBike.latitude = lastLng;
        currentBike.battery = percent;

        Telemetry t;
        t.id = generateUUID();
        t.bikeId = currentBike.userName;
        t.longitude = lastLng;
        t.latitude = lastLat;
        t.battery = percent;
        t.time = gsm.getUnixTimestamp();
        t.last_gps_contact_time = last_gps_contact_time;
        t.last_gps_lat = last_gps_lat;
        t.last_gps_long = last_gps_long;

        gsm.publishTelemetry(t, MQTT_TOPIC);
        Serial.println("Telemetry published.");
    }

    // Không dùng delay() nữa
}
/*
void loop()
{
    gpsUtil.update(); // luôn đọc GPS trước
    static unsigned long last = 0;
    if (millis() - last > 1000)
    {
        last = millis();

        gpsUtil.printDebug();

        float lat, lng;
        if (gpsUtil.getLocation(lat, lng))
        {
            isInside = false;
            last_gps_contact_time = gsm.getUnixTimestamp();
            last_gps_lat = lat;
            last_gps_long = lng;
        }
        else
        {
            lat = 0;
            lng = 0;
            last_gps_lat = lat;
            last_gps_long = lng;
            isInside = true;
        }
        if (isInside)
        {
            String cellJson = gsm.getCellTowerJSON();
            Serial.println(cellJson);
            if (cellJson.length() > 0)
            {
                String resp = gsm.httpPostJson("http://eu1.unwiredlabs.com/v2/process.php", cellJson);
                Serial.println("Location API response:");
                Serial.println(resp);
            }
        }
        if (!gsm.ensureMqttConnected("goscoot-bike-", "goscoot/control/bike001"))
        {
            delay(2000);
            return;
        }
        gsm.loop();

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
        delay(1000);
    }
} */

/*
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
    gpsUtil.printDebug();
    float lat = 0, lng = 0;

    if (gpsUtil.getLocation(lat, lng))
    {
        isInside = true;
        //last_gps_contact_time = gsm.getUnixTimestamp();
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

    delay(1000);

}
    */

/*
// Ví dụ với Arduino Mega: GPS nối vào Serial1 (RX1=19, TX1=18)
#define GPS_SERIAL Serial1

void setup() {
  Serial.begin(115200);
  GPS_SERIAL.begin(38400);
}

void loop() {
  while (GPS_SERIAL.available()) {
    char c = GPS_SERIAL.read();
    Serial.write(c);   // in nguyên dữ liệu từ GPS ra monitor
  }
}
  */

/*
TinyGPSPlus gps;
HardwareSerial &gpsPort = Serial1; // NEO-M10 TX -> RX1 (pin 19), RX -> TX1 (pin 18)

void setup()
{
    Serial.begin(115200);
    gpsPort.begin(38400); // đúng cái baud mà em đã test thấy NMEA đọc được
}

void loop()
{
    while (gpsPort.available() > 0)
    {
        char c = gpsPort.read();

        // echo raw NMEA để chắc chắn data vào đúng
        Serial.write(c);

        // cho TinyGPS++ ăn từng ký tự
        gps.encode(c);
    }

    static unsigned long last = 0;
    if (millis() - last > 1000)
    {
        last = millis();

        Serial.println();
        Serial.print("Chars: ");
        Serial.print(gps.charsProcessed());
        Serial.print(" | Sentences with fix: ");
        Serial.print(gps.sentencesWithFix());
        Serial.print(" | Age: ");
        Serial.println(gps.location.age());

        if (gps.location.isValid())
        {
            Serial.print("FIX: ");
            Serial.print(gps.location.lat(), 6);
            Serial.print(", ");
            Serial.println(gps.location.lng(), 6);
        }
        else
        {
            Serial.println("No valid GPS location yet.");
        }
    }
}
    */