#define TINY_GSM_MODEM_SIM7600

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
#include "NetworkTask/TerminateReservationWithServerMqtt.h"
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
#define HELMET_PIN 13

int qr_tx = A14;
int qr_rx = A15;

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(
    U8G2_R0,
    U8X8_PIN_NONE // reset pin not used
);

SoftwareSerial qrSerial(A14, A15);

Adafruit_INA219 ina219;

// GPS trên Serial1 (NEO-M10)
GpsConfiguration gpsConfiguration(&Serial1);

const String bikeUserName = "BIK_298A1J35";
const String bikePassworkd = "TrungLuong080699!!!";
String currentHub = "HUB-CXBN4HMN";
String qrContent = bikeUserName + "," + currentHub;

// ----------------- GSM / Network config -----------------
const char APN[] = "internet";
const char GPRS_USER[] = "";
const char GPRS_PASS[] = "";

// ----------------- MQTT config -----------------
const char *MQTT_HOST = "0.tcp.ap.ngrok.io";
const uint16_t MQTT_PORT = 15311;
const char *MQTT_USER = "BIK_298A1J35";
const char *MQTT_PASS = "TrungLuong080699!!!";
const char *MQTT_TOPIC = "/telemetry/BIK_298A1J35";
const char *ALERT_TOPIC = "alerts/BIK_298A1J35"; // alerts topic

const char *ALERT_TOPIC_TOPPLE = "alerts/topple/BIK_298A1J35";
const char *ALERT_TOPIC_GEOFENCE = "alerts/geofence/BIK_298A1J35";
const char *ALERT_TOPIC_BATTERY = "alerts/battery/BIK_298A1J35";

Alert *toppleAlert = nullptr;
Alert *lowBatteryAlert = nullptr;
Alert *geofenceAlert = nullptr;

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
QrScannerUtilityNonBlocking qrScanner(qrSerial);

// Network scheduler
NetworkInterfaceScheduler netScheduler;

int batteryLevel = 100;
float currentSpeedKmh = 0;
bool toBeUpdated = true;
DisplayPage currentPage = DisplayPage::QrScan;
DisplayPage prevPage = DisplayPage::QrScan;

DisplayTask displayTask(
    currentSpeedKmh,
    batteryLevel,
    currentPage,
    prevPage,
    toBeUpdated,
    app_logo_bitmap, // no default bitmap
    qrContent.c_str());

BatteryStateManager batteryManager(ina219, batteryLevel);

int16_t accelX = 0;
int16_t accelY = 0;
int16_t accelZ = 0;

int16_t gyroRollRate = 0;
int16_t gyroPitchRate = 0;
int16_t gyroYawRate = 0;

// -------------------------------
// State tracking (NEW)
// -------------------------------
VehicleState currentState = VehicleState::UPRIGHT;
unsigned long znStableSinceMs = 0;
float znRounded1dp = 0.0f;

// -------------------------------
// IMU configuration instance
// -------------------------------
ImuConfiguration imu(
    accelX, accelY, accelZ,
    gyroRollRate, gyroPitchRate, gyroYawRate,
    currentState,
    znStableSinceMs,
    znRounded1dp);

// =====================================================
//  GLOBAL CONFIG
// =====================================================

// ----------------- Objects / utilities -----------------

CellInfo cellInfo;

// GPS / bike status
int64_t currentUnixTime = 0;
bool isInside = false;

float last_gps_long = 106.754624;
float last_gps_lat = 10.8514327;
float cur_lng = 106.754624;
float cur_lat = 10.8514327;
int64_t last_gps_contact_time = 0;
OperationState operationState = OperationState::NORMAL;
UsageState usageState = UsageState::IDLE;
bool helmetIsConnected = true;
// trip id received from server
String currentTripId;

// in some .cpp
ValidateTripWithServerTaskMqtt *g_activeValidationTask = nullptr;
TerminateReservationWithServerMqtt *g_activeTripTerminationTask = nullptr;

void globalMqttCallback(char *topic, uint8_t *payload, unsigned int length)
{
    Serial.println(F("[MQTT] Message arrived"));
    if (g_activeValidationTask)
    {
        g_activeValidationTask->onMqttMessage(topic,
                                              (const uint8_t *)payload,
                                              length);
    }
    else if (g_activeTripTerminationTask)
    {
        g_activeTripTerminationTask->onMqttMessage(topic,
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

uint32_t bootCount = 0;

void printResetCause()
{
    uint8_t mcusr = MCUSR;
    MCUSR = 0; // clear flags

    Serial.print("[RESET] MCUSR=");
    Serial.println(mcusr, BIN);

    if (mcusr & _BV(BORF))
        Serial.println("[RESET] Brown-out (tụt áp)");
    if (mcusr & _BV(WDRF))
        Serial.println("[RESET] Watchdog");
    if (mcusr & _BV(EXTRF))
        Serial.println("[RESET] External reset pin");
    if (mcusr & _BV(PORF))
        Serial.println("[RESET] Power-on reset");
}

static bool lastStableConnected = true;
static uint8_t lastRead = LOW;
static unsigned long lastChangeMs = 0;

bool readHelmetConnectedDebounced()
{
    uint8_t r = digitalRead(HELMET_PIN);
    if (r != lastRead)
    {
        lastRead = r;
        lastChangeMs = millis();
    }

    if (millis() - lastChangeMs > 50)
    { // 50ms debounce
        return (lastRead == LOW);
    }

    // chưa ổn định -> trả trạng thái cũ
    return lastStableConnected;
}

bool batteryIsLow = false;
bool isToppled = false;
bool isCrashed = false;
bool isOutOfBound = false;

void testHttp()
{
    Serial.println("[HTTP] Starting HTTP test...");

    const char *host = "example.com";
    const int port = 80;
    const char *path = "/";

    if (!gsm.netClient.connect(host, port))
    {
        Serial.println("[HTTP] TCP connect FAILED");
        return;
    }

    Serial.println("[HTTP] TCP connected");

    // Send HTTP GET
    gsm.netClient.print(String("GET ") + path + " HTTP/1.1\r\n");
    gsm.netClient.print(String("Host: ") + host + "\r\n");
    gsm.netClient.print("Connection: close\r\n\r\n");

    // Read response
    unsigned long timeout = millis();
    while (gsm.netClient.connected() && millis() - timeout < 8000)
    {
        while (gsm.netClient.available())
        {
            char c = gsm.netClient.read();
            Serial.write(c);
            timeout = millis();
        }
    }

    gsm.netClient.stop();
    Serial.println("\n[HTTP] Done");
}

void setup()
{
    Serial.begin(115200);

    pinMode(HELMET_PIN, INPUT_PULLUP);
    delay(50);

    bootCount++;
    Serial.print("[BOOT] bootCount=");
    Serial.println(bootCount);

    Serial.println("Setting up...");
    Wire.begin(); // Mega I2C pins
    u8g2.begin(); // REQUIRED
    ina219.begin();
    batteryManager.begin();
    toBeUpdated = true; // force first draw
    Serial3.begin(9600);
    imu.begin();
    pinMode(STRAIGHT_LEFT, OUTPUT);
    pinMode(BACK_LEFT, OUTPUT);
    pinMode(STRAIGHT_RIGHT, OUTPUT);
    pinMode(BACK_RIGHT, OUTPUT);

    digitalWrite(STRAIGHT_LEFT, LOW);
    digitalWrite(BACK_LEFT, LOW);
    digitalWrite(STRAIGHT_RIGHT, LOW);
    digitalWrite(BACK_RIGHT, LOW);

    Dabble.begin(9600, Serial3);

    randomSeed(analogRead(A0));

    // QR Scanner (UART)
    qrScanner.begin(9600); // baud bạn đang dùng cho GM65 / MH-ET

    // GPS
    gpsConfiguration.begin(); // NEO-M10: 38400 bên trong GpsConfiguration */

    Serial.println("Setup Done");

    // GSM / Modem
    if (!gsm.setupModemBlocking())
    {
        Serial.println(F("[GSM] setup failed, will keep trying in loop"));
    };
    testHttp();
    Serial.println("Retrieving Time");
    // Sync time from modem
    timeConfig.syncOnceBlocking();

    Serial.println("[NET] TCP probe...");
    if (!gsm.netClient.connect(MQTT_HOST, MQTT_PORT))
    {
        Serial.println("[NET] TCP probe FAILED");
    }
    else
    {
        Serial.println("[NET] TCP probe OK");
        gsm.netClient.stop();
    }
    String clientId = String("goscoot-bike-") + String(random(0xffff), HEX);
    Serial.print("[MQTT] Connecting as ");
    Serial.println(clientId);

    bool ok = gsm.mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS);
    if (!ok)
    {
        Serial.print("[MQTT] connect failed, rc=");
        Serial.println(gsm.mqtt.state()); // -1/-2/-4...
    }
    else
    {
        Serial.println("[MQTT] connected");
    }
    gsm.mqtt.setCallback(globalMqttCallback);
}

// =====================================================
//  LOOP (non-blocking)
// =====================================================

void loop()
{
    currentUnixTime = timeConfig.nowUnixMs();
    unsigned long now = millis();
    static unsigned long lastToppleAlert = 0;

    
    if (now - lastToppleAlert > 1000UL)
    {
        lastToppleAlert = now;
        // Update IMU (imu.update() sẽ tự update currentState theo logic bạn đã viết)
        if (imu.update())
        {
            // 1) Chỉ gửi alert khi KHÔNG UPRIGHT
            if (currentState != VehicleState::UPRIGHT)
            {
                if (toppleAlert == nullptr)
                {
                    Serial.println(F("[IMU] Vehicle not upright, sending TOPPLE alert"));
                    // 2) Tạo alert
                    Alert alert;
                    alert.id = generateUUID();
                    alert.bike_id = bikeUserName;              // hoặc bikeId tùy struct bạn
                    alert.content = "Xe BIK_298A1J35 có dấu hiệu bị lật. Xin vui lòng kiểm tra"; // bạn đổi message tùy ý
                    alert.type = AlertType::TOPPLE;            // nếu bạn có enum này
                    alert.longitude = cur_lng;                 // từ input của bạn
                    alert.latitude = cur_lat;                  // từ input của bạn
                    alert.time = currentUnixTime;              // unix time bạn đang dùng

                    // (khuyến nghị) include state để backend hiểu nguyên nhân
                    // alert.state = (int)currentState;            // nếu struct Alert có field

                    // 3) Encode
                    uint8_t alertBuf[256];
                    int alertLen = encodeAlert(alert, alertBuf);
                    isToppled = true;

                    toppleAlert = &alert;

                    // 4) Publish MQTT qua scheduler
                    NetworkTask *alertTask = new PublishMqttTask(
                        gsm,
                        alertBuf,
                        alertLen,
                        ALERT_TOPIC_TOPPLE);

                    netScheduler.enqueue(alertTask, TASK_PRIORITY_CRITICAL);
                }
            }
            else
            {
                toppleAlert = nullptr;
                isToppled = false;
                Serial.println(F("[IMU] Vehicle is upright"));
            }
        }
        else
        {
            Serial.println(F("[IMU] Not initialized / update failed"));
        }
    }
        
        

    


    batteryManager.update();
        if (batteryLevel <= 49)
        {
            currentPage = DisplayPage::LowBatteryAlert;
            //Serial.println(F("[ALERT] Low battery zone, enqueue alert"));
            Alert alert;
            alert.id = generateUUID();
            alert.bike_id = bikeUserName;
            alert.content = "Bike outside geofence";
            alert.type = AlertType::LOW_BATTERY;
            alert.longitude = cur_lng;
            alert.latitude = cur_lat;
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
    
    
    
    digitalWrite(STRAIGHT_LEFT, LOW);
    digitalWrite(STRAIGHT_RIGHT, LOW);
    digitalWrite(BACK_LEFT, LOW);
    digitalWrite(BACK_RIGHT, LOW);
    Dabble.processInput();
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
    {
        Serial.println("LEFT");
    }

    if (GamePad.isRightPressed())
    {
        Serial.println("RIGHT");
    }

    if (GamePad.isSquarePressed())
    {
        Serial.println("SQUARE");
    }

    if (GamePad.isCirclePressed())
    {
        Serial.println("CIRCLE");
    }

    if (GamePad.isTrianglePressed())
    {
        Serial.println("TRIANGLE");
    }

    if (GamePad.isCrossPressed())
    {
        Serial.println("CROSS");
    }

    if (g_activeValidationTask->isCompleted())
    {
        g_activeValidationTask = nullptr;
    }
    if (g_activeTripTerminationTask->isCompleted())
    {
        g_activeTripTerminationTask = nullptr;
    }
    

    // -------------------------------------------------
    // 1) QR SCANNER – ONLY when bike is IDLE
    // -------------------------------------------------

    
    qrScanner.step();

    if (qrScanner.isScanReady())
    {
        Serial.println("QR FOUND");
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
            currentPage = DisplayPage::PleaseWait;
            prevPage = DisplayPage::QrScan;
            Trip trip = Trip();
            trip.current_lat = last_gps_lat;
            trip.current_lng = last_gps_long;
            bool ok = parseTripJson(qrCode, trip);

            if (ok)
            {
                Serial.println(F("[QR] Valid Trip JSON"));
                const char *request = "/reservation/BIK_298A1J35/validate";
                static char responseTopic[64];

                snprintf(
                    responseTopic,
                    sizeof(responseTopic),
                    "/reservation/%s/update",
                    trip.id.c_str());

                const char *response = responseTopic;
                Serial.println(response);
                NetworkTask *task = new ValidateTripWithServerTaskMqtt(
                    gsm,
                    trip,
                    request,
                    response,
                    currentTripId,
                    usageState,
                    currentPage,
                    prevPage,
                    toBeUpdated);

                netScheduler.enqueue(task, TASK_PRIORITY_CRITICAL);
            }
            else
            {
                Serial.println(F("[QR] Invalid Trip JSON"));
                currentPage = DisplayPage::IncorrectQrScan;
                prevPage = DisplayPage::QrScan;
            }
        }
    }
    

    
    bool helmetConnected = readHelmetConnectedDebounced();

    // detect rising edge: false -> true
    if (helmetConnected)
    {
        // helmet vừa mới được "cắm vào"
        if (usageState == UsageState::INUSED)
        {
            if (currentTripId.length() > 0)
            {
                helmetIsConnected = true;
                currentPage = DisplayPage::TripConclusion;
                prevPage = DisplayPage::QrScan;
                static char requestTopic[96];

                snprintf(
                    requestTopic,
                    sizeof(requestTopic),
                    "/reservation/%s/%s/termination",
                    bikeUserName.c_str(),
                    currentTripId.c_str());

                const char *request = requestTopic;
                Serial.println(request);

                static char responseTopic[64];
                snprintf(
                    responseTopic,
                    sizeof(responseTopic),
                    "/reservation/%s/update",
                    currentTripId.c_str());

                const char *response = responseTopic;
                Serial.println(response);

                TripTerminationPayload tripTerminationPayload = {
                    .end_lng = cur_lng,
                    .end_lat = cur_lat};

                NetworkTask *task = new TerminateReservationWithServerMqtt(
                    gsm,
                    tripTerminationPayload,
                    request,
                    response,
                    currentTripId,
                    usageState,
                    currentPage,
                    prevPage,
                    toBeUpdated);

                netScheduler.enqueue(task, TASK_PRIORITY_CRITICAL);
                usageState = UsageState::IDLE;
            }
        }
    }
    else
    {
        if (currentTripId.length() > 0 && usageState != UsageState::INUSED)
        {
            toBeUpdated = true;
            usageState = UsageState::INUSED;
            Serial.println(F("[HELMET] Helmet removed, bike IN_USED"));
            currentPage = DisplayPage::Welcome;
            prevPage = DisplayPage::QrScan;
        }
    }

    lastStableConnected = helmetConnected;


    // -------------------------------------------------
    // 2) GPS – cập nhật vị trí, không đụng LCD
    // -------------------------------------------------

    gpsConfiguration.update();

    static unsigned long lastGpsPrint = 0;
    static float lastLat = 10.85766, lastLng = 106.76659;
    static unsigned long last_geolocation = 0;
    last_gps_contact_time = currentUnixTime;

    
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
               /*
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
                   */
                   

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
        t.longitude = cur_lng;
        t.latitude = cur_lat;
        t.battery = batteryLevel;
        t.time = currentUnixTime;
        t.last_gps_contact_time = last_gps_contact_time;
        t.last_gps_lat = last_gps_lat;
        t.last_gps_long = last_gps_long;
        t.batteryIsLow = batteryIsLow;
        t.isCrashed = isCrashed;
        t.isToppled = isToppled;
        t.isOutOfBound = isOutOfBound;
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
        Serial.println("Maintaining mqtt connection....");
        // Chỉ thêm nếu còn chỗ, và không cần eviction
        netScheduler.enqueueIfSpace(
            new MqttMaintenanceTask(gsm),
            TASK_PRIORITY_LOW);

        /*
    netScheduler.enqueueIfSpace(
        new HttpMaintenanceTask(http),
        TASK_PRIORITY_LOW);
        */
        
    }
        
    

    displayTask.display();
}
