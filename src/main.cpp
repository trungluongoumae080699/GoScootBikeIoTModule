#define TINY_GSM_MODEM_SIM7600 // MUST be first

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <time.h>
#include <SoftwareSerial.h>
#include <Dabble.h>

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
#include "NetworkTask/HttpMaintenanceTask.h"
#include "NetworkTask/MqttMaintenanceTask.h"
#include "NetworkTask/ValidateReservationWithServerMqtt.h"
#include "BatteryManagement/BatteryStateManager.h"
#include "ImuConfiguration/ImuConfiguraton.h"

#include <Wire.h>
#include <U8g2lib.h>
#include <qrcode.h>
#include <UI/DisplayTask.h>
#include <UI/App_logo.h>

#define STRAIGHT_LEFT A7
#define BACK_LEFT A6
#define STRAIGHT_RIGHT A5
#define BACK_RIGHT A4

int qr_tx = A14;
int qr_rx = A15;

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(
    U8G2_R0,
    U8X8_PIN_NONE // reset pin not used
);

SoftwareSerial qrSerial(15, 14);
Adafruit_INA219 ina219;

// GPS trên Serial1 (NEO-M10)
GpsConfiguration gpsConfiguration(&Serial1);

const String bikeUserName = "BIK_298A1J35";
const String bikePassworkd = "TrungLuong080699!!!";
String currentHub = "HUB-5XRGFY6Z";

// ----------------- GSM / Network config -----------------
const char APN[] = "v-internet";
const char GPRS_USER[] = "";
const char GPRS_PASS[] = "";

// ----------------- MQTT config -----------------
const char *MQTT_HOST = "0.tcp.ap.ngrok.io";
const uint16_t MQTT_PORT = 16341;
const char *MQTT_USER = "BIK_298A1J35";
const char *MQTT_PASS = "TrungLuong080699!!!";
const char *MQTT_TOPIC = "/telemetry/BIK_298A1J35";
const char *ALERT_TOPIC = "alerts/BIK_298A1J35"; // alerts topic

// GSM configuration (Serial2 = modem)
GsmConfiguration gsm(
    Serial2,
    APN, GPRS_USER, GPRS_PASS,
    MQTT_HOST, MQTT_PORT,
    MQTT_USER, MQTT_PASS);

// HTTP utility (dùng netClient + mqtt bên trong gsm)
HttpConfiguration http(gsm.netClient, &gsm.mqtt);

// Time from modem
TimeConfiguration timeConfig(gsm.modem);

// QR scanner (GM65 hoặc MH-ET Live) trên Serial3
QrScannerUtilityNonBlocking qrScanner(qrScanner);

// Network scheduler
NetworkInterfaceScheduler netScheduler;

int batteryLevel = 100;
float currentSpeedKmh = 0;
bool toBeUpdated = true;
DisplayPage currentPage = DisplayPage::QrScan;

DisplayTask displayTask(
    currentSpeedKmh,
    batteryLevel,
    currentPage,
    toBeUpdated,
    nullptr,          // no default bitmap
    "Scan to ride..." // example QR text
);

BatteryStateManager batteryManager(ina219, batteryLevel);

int16_t accelX, accelY, accelZ;
int16_t gyroRollRate, gyroPitchRate, gyroYawRate;

ImuConfiguration imu(
    accelX, accelY, accelZ,
    gyroRollRate, gyroPitchRate, gyroYawRate);

// =====================================================
//  GLOBAL CONFIG
// =====================================================

// ----------------- Objects / utilities -----------------

CellInfo cellInfo;

// GPS / bike status
int64_t currentUnixTime = 0;
bool isInside = false;
float last_gps_long = 0;
float last_gps_lat = 0;
float cur_lng = 0;
float cur_lat = 0;
int64_t last_gps_contact_time = 0;
OperationState operationState = OperationState::NORMAL;
UsageState usageState = UsageState::IDLE;
// trip id received from server
String currentTripId;
String lcdDisplayLine1 = "Scan QR Code";
String lcdDisplayLine2 = "To start trip!";

// in some .cpp
ValidateTripWithServerTaskMqtt *g_activeValidationTask = nullptr;

void globalMqttCallback(char *topic, uint8_t *payload, unsigned int length)
{
    Serial.println(F("[MQTT] Message arrived"));
    if (g_activeValidationTask)
    {
        g_activeValidationTask->onMqttMessage(topic,
                                              (const uint8_t *)payload,
                                              length);
    }
}

// =====================================================
//  Helper functions
// =====================================================
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
    Serial.begin(115200);
    ina219.begin();
    batteryManager.begin();
    Wire.begin();       // Mega I2C pins
    u8g2.begin();       // REQUIRED
    toBeUpdated = true; // force first draw
    imu.begin();
    Serial3.begin(9600);
    pinMode(STRAIGHT_LEFT, OUTPUT);
    pinMode(BACK_LEFT, OUTPUT);
    pinMode(STRAIGHT_RIGHT, OUTPUT);
    pinMode(BACK_RIGHT, OUTPUT);

    digitalWrite(STRAIGHT_LEFT, LOW);
    digitalWrite(BACK_LEFT, LOW);
    digitalWrite(STRAIGHT_RIGHT, LOW);
    digitalWrite(BACK_RIGHT, LOW);
    Serial.begin(115200);
    Dabble.begin(9600, Serial3);

    randomSeed(analogRead(A0));

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
    timeConfig.syncOnceBlocking();
    gsm.mqtt.connected();
    gsm.mqtt.setCallback(globalMqttCallback);
}

// =====================================================
//  LOOP (non-blocking)
// =====================================================

void loop()
{
    imu.update();
    batteryManager.update();
    if (batteryLevel <= 49)
    {
        currentPage = DisplayPage::LowBatteryAlert;
        Serial.println(F("[ALERT] Low battery zone, enqueue alert"));
        Alert alert;
        alert.id = generateUUID();
        alert.bike_id = bikeUserName;
        alert.content = "Bike outside geofence";
        alert.type = AlertType::LOW_BATTERY;
        alert.longitude = cur_lng;
        alert.latitude = cur_lat;
        alert.time = currentUnixTime;
        operationState = OUT_OF_BOUND;
        uint8_t alertBuf[256];
        int alertLen = encodeAlert(alert, alertBuf);
        NetworkTask *alertTask = new PublishMqttTask(
            gsm,
            alertBuf,
            alertLen,
            ALERT_TOPIC);
        netScheduler.enqueue(alertTask, TASK_PRIORITY_CRITICAL);
    }
    digitalWrite(STRAIGHT_LEFT, LOW);
    digitalWrite(STRAIGHT_RIGHT, LOW);
    digitalWrite(BACK_LEFT, LOW);
    digitalWrite(BACK_RIGHT, LOW);
    Dabble.processInput();
    // Read from HM-10 → Serial Monitor

    currentUnixTime = timeConfig.nowUnixMs();
    unsigned long now = millis();

    if (g_activeValidationTask->isCompleted())
    {
        g_activeValidationTask = nullptr;
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
        if (usageState != IDLE)
        {
            Serial.println(F("[QR] Ignored: bike already IN_USE"));
        }
        else
        {
            Trip trip = Trip();
            trip.current_lat = last_gps_lat;
            trip.current_lng = last_gps_long;
            bool ok = parseTripJson(qrCode, trip);

            if (ok)
            {
                Serial.println(F("[QR] Valid Trip JSON"));
                // ƯU TIÊN Reservation validation trên HTTP
                // Enqueue reservation validation request - task priority critical
                NetworkTask *task = new ValidateTripWithServerTaskMqtt(
                    gsm,
                    trip,
                    "/reservation/BIK_298A1J35/validate",
                    "/reservation/BIK_298A1J35/response",
                    currentTripId,
                    usageState,
                    currentPage);
                netScheduler.enqueue(task, TASK_PRIORITY_CRITICAL);
            }
            else
            {
                Serial.println(F("[QR] Invalid Trip JSON"));
            }
        }
    }

    // -------------------------------------------------
    // 2) GPS – cập nhật vị trí, không đụng LCD
    // -------------------------------------------------
    gpsConfiguration.update();

    static unsigned long lastGpsPrint = 0;
    static float lastLat = 0, lastLng = 0;
    static unsigned long last_geolocation = 0;

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
                currentPage = DisplayPage::BoundaryCrossAlert;
                Serial.println(F("[ALERT] Outside allowed boundary, enqueue alert"));

                Alert alert;
                alert.id = generateUUID();
                alert.bike_id = bikeUserName;
                alert.content = "Bike outside geofence";
                alert.type = AlertType::BOUNDARY_CROSS;
                alert.longitude = lng;
                alert.latitude = lat;
                alert.time = currentUnixTime;
                operationState = OUT_OF_BOUND;

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
            Serial.println(F("[GPS] No fix yet"));
            isInside = true;
        }

        // -------------------------------------------------
        // 3) Indoor logic – enqueue cell / geolocation tasks
        // -------------------------------------------------
        if (isInside && now - last_geolocation >= 10000)
        {

            if (cellInfo.isOutdated)
            {

                NetworkTask *cellTask = new CellTowerQueryTask(
                    gsm,
                    cellInfo);
                netScheduler.enqueue(cellTask, TASK_PRIORITY_LOW);
            }
            else
            {
                // we have a CellInfo → call UnwiredLabs for approximate location
                Serial.println(F("[INDOOR] Have CellInfo, enqueue QueryGeolocationApiTask"));

                NetworkTask *geoTask = new QueryGeolocationApiTask(
                    http,
                    cellInfo,
                    cur_lat,
                    cur_lng);
                netScheduler.enqueue(geoTask, TASK_PRIORITY_LOW);
            }
        }
    }

    // -------------------------------------------------
    // 6) PUBLISH TELEMETRY MỖI 5 GIÂY – via scheduler
    // -------------------------------------------------
    static unsigned long lastTelemetry = 0;
    if (now - lastTelemetry >= 5000)
    {
        lastTelemetry = now;

        // float vbatt = readBatteryVoltage();
        Telemetry t;
        t.id = generateUUID();
        t.bikeId = bikeUserName;
        t.longitude = lastLng;
        t.latitude = lastLat;
        t.battery = batteryLevel;
        t.time = currentUnixTime;
        t.last_gps_contact_time = last_gps_contact_time;
        t.last_gps_lat = last_gps_lat;
        t.last_gps_long = last_gps_long;
        t.operationState = operationState;
        t.usageState = usageState;

        uint8_t buffer[256];
        int payloadLen = encodeTelemetry(t, buffer);

        Serial.println(F("[TEL] Enqueue telemetry publish"));

        NetworkTask *teleTask = new PublishMqttTask(
            gsm,
            buffer,
            payloadLen,
            MQTT_TOPIC);
        // Telemetry is low priority / skippable
        netScheduler.enqueue(teleTask, TASK_PRIORITY_NORMAL);
    }

    // -------------------------------------------------
    // 7) Run one network task from scheduler
    // -------------------------------------------------
    netScheduler.step();

    static unsigned long lastMaint = 0;
    if (millis() - lastMaint > 200) // mỗi 200ms bơm 1 lần cho nhẹ nhàng
    {
        lastMaint = millis();

        // Chỉ thêm nếu còn chỗ, và không cần eviction
        netScheduler.enqueueIfSpace(
            new MqttMaintenanceTask(gsm),
            TASK_PRIORITY_LOW);

        netScheduler.enqueueIfSpace(
            new HttpMaintenanceTask(http),
            TASK_PRIORITY_LOW);
    }
    displayTask.display();

    if (GamePad.isUpPressed())
    {
        if (usageState == UsageState::INUSED)
        {
            digitalWrite(STRAIGHT_LEFT, HIGH);
            digitalWrite(STRAIGHT_RIGHT, HIGH);
            digitalWrite(BACK_LEFT, LOW);
            digitalWrite(BACK_RIGHT, LOW);
        }
    }
    if (GamePad.isDownPressed())
    {
        if (usageState == UsageState::INUSED)
        {
            digitalWrite(STRAIGHT_LEFT, LOW);
            digitalWrite(STRAIGHT_RIGHT, LOW);
            digitalWrite(BACK_LEFT, HIGH);
            digitalWrite(BACK_RIGHT, HIGH);
        }
    }
    if (GamePad.isLeftPressed())
        Serial.println("LEFT");
    if (GamePad.isRightPressed())
        Serial.println("RIGHT");

    if (GamePad.isSquarePressed())
        Serial.println("SQUARE");
    if (GamePad.isCirclePressed())
        Serial.println("CIRCLE");
    if (GamePad.isTrianglePressed())
        Serial.println("TRIANGLE");
    if (GamePad.isCrossPressed())
        Serial.println("CROSS");
}