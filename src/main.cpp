#define TINY_GSM_MODEM_SIM7600 // MUST be first

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <time.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

#include "Domains/Bike.h"
#include "Domains/Telemetry.h"
#include "Domains/CellInfo.h"
#include "Domains/Alert.h" // <-- where Alert + encodeAlert live
#include "Domains/Trip.h"
#include "GpsConfiguration/GpsConfiguration.h"
#include "NetworkConfiguration/GsmConfiguration.h"
#include "NetworkConfiguration/HttpConfiguration.h"
#include "TimeConfiguration/TimeConfiguration.h"
#include "QrScannerConfiguration/QrScannerUtilityNonBlocking.h"

// Scheduler + Tasks
#include "NetworkConfiguration/NetworkQueue.h"
#include "NetworkTask/PublishMqttTask.h"
#include "NetworkTask/CellTowerQueryTask.h"
#include "NetworkTask/FetchGeolocationApiTask.h"
#include "NetworkTask/ValidateReservationWithServer.h"

// =====================================================
//  GLOBAL CONFIG
// =====================================================

// ----------------- GSM / Network config -----------------
const char APN[] = "v-internet";
const char GPRS_USER[] = "";
const char GPRS_PASS[] = "";

// ----------------- MQTT config -----------------
const char *MQTT_HOST = "0.tcp.ap.ngrok.io";
const uint16_t MQTT_PORT = 12123;
const char *MQTT_USER = "BIK_298A1J35";
const char *MQTT_PASS = "TrungLuong080699!!!";
const char *MQTT_TOPIC = "/telemetry/BIK_298A1J35";
const char *ALERT_TOPIC = "/alerts/BIK_298A1J35"; // alerts topic

// ----------------- Objects / utilities -----------------

Bike currentBike;
CellInfo *cellInfo = nullptr; // will be allocated on demand

// I2C LCD 16x2 @ 0x27
LiquidCrystal_I2C lcd(0x27, 16, 2);

// GPS trên Serial1 (NEO-M10)
GpsConfiguration gpsConfiguration(&Serial1);

// GSM configuration (Serial2 = modem)
GsmConfiguration gsm(
    Serial2,
    APN, GPRS_USER, GPRS_PASS,
    MQTT_HOST, MQTT_PORT,
    MQTT_USER, MQTT_PASS);

// HTTP utility (dùng netClient + mqtt bên trong gsm)
HttpConfiguration http(gsm.netClient, &gsm.mqtt);

// Time from modem
TimeConfiguration time(gsm.modem);

// QR scanner (GM65 hoặc MH-ET Live) trên Serial3
QrScannerUtilityNonBlocking qrScanner(Serial3);

// Network scheduler
NetworkInterfaceScheduler netScheduler;

SoftwareSerial BT(10, 11); // RX = 10, TX = 11
const int IN1 = 8;
const int IN2 = 9;

// Battery sensor (nếu cần sau này)
int sensorPin = A0; // OUT từ module đo áp
float RATIO = 5.0;  // tỉ lệ chia áp

// GPS / bike status
int64_t currentUnixTime = 0;
bool isInside = false;
float last_gps_long = 0;
float last_gps_lat = 0;
float cur_lng = 0;
float cur_lat = 0;
int64_t last_gps_contact_time = 0;
BikeState bikeState = IDLE;

// trip id received from server
String currentTripId;

// =====================================================
//  Helper functions
// =====================================================

float readBatteryVoltage()
{
    int raw = analogRead(sensorPin);
    float voltage = (raw * 5.0f / 1023.0f); // 0–5V trên A0
    return voltage * RATIO;                 // trả về áp thực tế của pack
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

    String uuid;
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

// Placeholder geofence check – implement real logic later
bool isOutsideAllowedArea(float lat, float lng)
{
    // TODO: your geofence logic here.
    // For now always "inside".
    (void)lat;
    (void)lng;
    return false;
}

// =====================================================
//  SETUP
// =====================================================

void setup()
{
    BT.begin(9600); // HC-06 default baud rate
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    // stop motors at start
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    Serial.begin(115200);
    delay(200);

    // LCD
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Scan QR Code");
    lcd.setCursor(0, 1);
    lcd.print("to start trip!");

    randomSeed(analogRead(A0));

    currentBike.userName = "BIK_298A1J35";
    currentBike.password = "TrungLuong080699!!!";
    currentBike.battery = 100;

    // QR Scanner (UART)
    qrScanner.begin(9600); // baud bạn đang dùng cho GM65 / MH-ET

    // GPS
    gpsConfiguration.begin(); // NEO-M10: 38400 bên trong GpsConfiguration

    // GSM / Modem
    if (!gsm.setupModemBlocking())
    {
        Serial.println(F("[GSM] setup failed, will keep trying in loop"));
    }

    // Sync time from modem
    time.syncOnceBlocking();
}

// =====================================================
//  LOOP (non-blocking)
// =====================================================

void loop()
{
    currentUnixTime = time.nowUnixMs();
    unsigned long now = millis();

    if (BT.available())
    {
        char cmd = BT.read();
        if (cmd == '1' && bikeState == INUSED)
        {

            // Forward
            digitalWrite(IN1, HIGH);
            digitalWrite(IN2, LOW);
        }
        else if (cmd == '2' && bikeState == INUSED)
        {
            // Backward
            digitalWrite(IN1, LOW);
            digitalWrite(IN2, HIGH);
        }
        else 
        {
            // Stop
            digitalWrite(IN1, LOW);
            digitalWrite(IN2, LOW);
        }
    } else {
        digitalWrite(IN1, LOW);
        digitalWrite(IN2, LOW);
    }

    // -------------------------------------------------
    // 1) QR SCANNER – ONLY when bike is IDLE
    // -------------------------------------------------
    qrScanner.step();

    if (qrScanner.isScanReady())
    {
        String qrCode = qrScanner.takeResult();
        Serial.print(F("[QR] JSON: "));
        Serial.println(qrCode);

        // Nếu xe đang IN_USE thì không cho scan
        if (bikeState != IDLE)
        {
            Serial.println(F("[QR] Ignored: bike already IN_USE"));
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Bike in use");
            lcd.setCursor(0, 1);
            lcd.print("Cannot scan");
        }
        else
        {
            bool ok = validateTripJson(qrCode);
            if (ok)
            {
                Serial.println(F("[QR] Valid Trip JSON"));
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("QR OK");
                lcd.setCursor(0, 1);
                lcd.print("Validating...");

                // ƯU TIÊN Reservation validation trên HTTP
                // Enqueue reservation validation request - task priority critical
                NetworkTask *task = new ValidateTripWithServerTask(
                    http,
                    "http://your-backend/trip/validate",
                    qrCode,
                    &currentTripId, &bikeState);
                netScheduler.enqueue(task, TASK_PRIORITY_CRITICAL);
            }
            else
            {
                Serial.println(F("[QR] Invalid Trip JSON"));
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Invalid QR");
                lcd.setCursor(0, 1);
                lcd.print("Please try again");
            }
        }
    }

    // -------------------------------------------------
    // 2) GPS – cập nhật vị trí, không đụng LCD
    // -------------------------------------------------
    gpsConfiguration.update();

    static unsigned long lastGpsPrint = 0;
    static float lastLat = 0, lastLng = 0;

    if (now - lastGpsPrint >= 1000)
    {
        lastGpsPrint = now;
        gpsConfiguration.printDebug();
        float lat, lng;
        if (gpsConfiguration.getLocation(lat, lng))
        {
            isInside = false;
            last_gps_lat = lat;
            last_gps_long = lng;
            cur_lat = lat;
            cur_lng = lng;
            lastLat = lat;
            lastLng = lng;
            last_gps_contact_time = currentUnixTime;

            // Check if location is inside allowed area
            if (isOutsideAllowedArea(lat, lng))
            {
                Serial.println(F("[ALERT] Outside allowed boundary, enqueue alert"));

                Alert alert;
                alert.id = generateUUID();
                alert.bike_id = currentBike.userName;
                alert.content = "Bike outside geofence";
                alert.type = AlertType::BOUNDARY_CROSS;
                alert.longitude = lng;
                alert.latitude = lat;
                alert.time = currentUnixTime;

                uint8_t alertBuf[256];
                int alertLen = encodeAlert(alert, alertBuf);

                NetworkTask *alertTask = new PublishMqttTask(
                    gsm,
                    alertBuf,
                    alertLen,
                    ALERT_TOPIC);
                netScheduler.enqueue(alertTask, TASK_PRIORITY_CRITICAL);
            }
        }
        else
        {
            // không có GPS fix → indoor candidate
            isInside = true;
        }
    }

    // -------------------------------------------------
    // 3) Indoor logic – enqueue cell / geolocation tasks
    // -------------------------------------------------
    if (isInside)
    {
        // if we don't yet have CellInfo, query CPSI first
        if (cellInfo == nullptr)
        {
            Serial.println(F("[INDOOR] No CellInfo yet, enqueue CellTowerQueryTask"));
            cellInfo = new CellInfo();

            NetworkTask *cellTask = new CellTowerQueryTask(
                gsm,
                *cellInfo);
            netScheduler.enqueue(cellTask, TASK_PRIORITY_NORMAL);
        }
        else
        {
            // we have a CellInfo → call UnwiredLabs for approximate location
            Serial.println(F("[INDOOR] Have CellInfo, enqueue QueryGeolocationApiTask"));

            NetworkTask *geoTask = new QueryGeolocationApiTask(
                http,
                cellInfo,
                &cur_lat,
                &cur_lng);
            netScheduler.enqueue(geoTask, TASK_PRIORITY_NORMAL);
        }
    }

    // -------------------------------------------------
    // 4) MQTT keep-alive
    // -------------------------------------------------
    gsm.stepMqtt();

    // -------------------------------------------------
    // 5) HTTP step (if your HttpConfiguration is non-blocking)
    // -------------------------------------------------
    http.stepHttp();

    // -------------------------------------------------
    // 6) PUBLISH TELEMETRY MỖI 5 GIÂY – via scheduler
    // -------------------------------------------------
    static unsigned long lastTelemetry = 0;
    if (now - lastTelemetry >= 5000)
    {
        lastTelemetry = now;

        // float vbatt = readBatteryVoltage();
        int percent = 100;

        currentBike.longitude = lastLng;
        currentBike.latitude = lastLat;
        currentBike.battery = percent;

        Telemetry t;
        t.id = generateUUID();
        t.bikeId = currentBike.userName;
        t.longitude = lastLng;
        t.latitude = lastLat;
        t.battery = percent;
        t.time = currentUnixTime;
        t.last_gps_contact_time = last_gps_contact_time;
        t.last_gps_lat = last_gps_lat;
        t.last_gps_long = last_gps_long;

        uint8_t buffer[256];
        int payloadLen = encodeTelemetry(t, buffer);

        Serial.println(F("[TEL] Enqueue telemetry publish"));

        NetworkTask *teleTask = new PublishMqttTask(
            gsm,
            buffer,
            payloadLen,
            MQTT_TOPIC);
        // Telemetry is low priority / skippable
        netScheduler.enqueueIfSpace(teleTask, TASK_PRIORITY_LOW);
    }

    // -------------------------------------------------
    // 7) Run one network task from scheduler
    // -------------------------------------------------
    netScheduler.step();

    // ❌ Không dùng delay() to đùng ở cuối loop
}