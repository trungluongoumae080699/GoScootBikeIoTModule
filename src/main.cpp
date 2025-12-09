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

#define STRAIGHT_IN1 4
#define STRAIGHT_IN2 7
#define BACK_IN1 5
#define BACK_IN2 6


U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(
    U8G2_R0,
    U8X8_PIN_NONE  // reset pin not used
);

SoftwareSerial qrSerial(15, 14);
Adafruit_INA219 ina219;

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
    nullptr, // no default bitmap
    "Scan to ride..." // example QR text
);

BatteryStateManager batteryManager(ina219, batteryLevel);

int16_t accelX, accelY, accelZ;
int16_t gyroRollRate, gyroPitchRate, gyroYawRate;

ImuConfiguration imu(
    accelX, accelY, accelZ,
    gyroRollRate, gyroPitchRate, gyroYawRate
);


// =====================================================
//  GLOBAL CONFIG
// =====================================================

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

// ----------------- Objects / utilities -----------------

Bike currentBike;
CellInfo cellInfo;



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
    Wire.begin();               // Mega I2C pins
    u8g2.begin();               // REQUIRED
    toBeUpdated = true;         // force first draw
    imu.begin();
    
    /* BT.begin(9600); // HC-06 default baud rate
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    // stop motors at start
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    delay(200);

    // LCD
    lcd.init();
    lcd.backlight();
    lcd.clear();

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
    timeConfig.syncOnceBlocking();
    gsm.mqtt.connected();
    gsm.mqtt.setCallback(globalMqttCallback); */
}

// =====================================================
//  LOOP (non-blocking)
// =====================================================

void loop()
{
    imu.update();
    batteryManager.update();

    /*  currentUnixTime = timeConfig.nowUnixMs();
     unsigned long now = millis();
     lcd.setCursor(0, 0);
     lcd.print(lcdDisplayLine1);
     lcd.setCursor(0, 1);
     lcd.print(lcdDisplayLine2);

     if (g_activeValidationTask->isCompleted()){
         g_activeValidationTask = nullptr;
     }

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
     }
     else
     {
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
             Trip trip = Trip();
             trip.current_lat = last_gps_lat;
             trip.current_lng = last_gps_long;
             bool ok = parseTripJson(qrCode, trip);

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
                 NetworkTask *task = new ValidateTripWithServerTaskMqtt(
                     gsm,
                     trip,
                     "/reservation/BIK_298A1J35/validate",
                     "/reservation/BIK_298A1J35/response",
                     currentTripId,
                     bikeState,
                     lcdDisplayLine1,
                     lcdDisplayLine2);
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
     static unsigned long last_geolocation = 0; */

    /* if (now - lastGpsPrint >= 1000)
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

    } */

    // -------------------------------------------------
    // 6) PUBLISH TELEMETRY MỖI 5 GIÂY – via scheduler
    // -------------------------------------------------
    /*     static unsigned long lastTelemetry = 0;
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
            netScheduler.enqueue(teleTask, TASK_PRIORITY_NORMAL);
        } */

    // -------------------------------------------------
    // 7) Run one network task from scheduler
    // -------------------------------------------------
    /*  netScheduler.step();


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
     }   */
    displayTask.display();
}