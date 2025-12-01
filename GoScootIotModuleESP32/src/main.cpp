#include <Arduino.h>
#include <ESP32QRCodeReader.h>

// ESP32-CAM AI-Thinker pin map
ESP32QRCodeReader reader(CAMERA_MODEL_AI_THINKER);

// FreeRTOS task that will read QR codes
void onQrCodeTask(void *pvParameters) {
    QRCodeData qrCodeData;

    Serial.print("onQrCodeTask running on core ");
    Serial.println(xPortGetCoreID());

    while (true) {
        // Try to get a QR code within 100 ms
        if (reader.receiveQrCode(&qrCodeData, 100)) {
            Serial.println("🔔 Found QRCode");

            if (qrCodeData.valid) {
                Serial.print("✅ Payload: ");
                Serial.println((const char *)qrCodeData.payload);
            } else {
                Serial.print("❌ Invalid: ");
                Serial.println((const char *)qrCodeData.payload);
            }

            // tránh spam nếu giữ QR trước camera
            vTaskDelay(1500 / portTICK_PERIOD_MS);
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("=== ESP32-CAM QR Scanner (ESP32QRCodeReader) ===");

    // Optional: check PSRAM – this lib needs it
    if (psramFound()) {
        Serial.println("✅ PSRAM detected");
    } else {
        Serial.println("⚠️ PSRAM NOT FOUND – this lib may not work on this board");
    }

    Serial.println("Calling reader.setup()...");
    reader.setup();                        // init camera + QR engine
    Serial.println("reader.setup() done");

    Serial.println("Starting internal QR task on core 1...");
    reader.beginOnCore(1);                 // start camera/decoder task
    Serial.println("reader.beginOnCore(1) done");

    // Start our QR consumer task
    xTaskCreate(
        onQrCodeTask,                      // task function
        "onQrCode",                        // name
        4 * 1024,                          // stack
        NULL,                              // params
        4,                                 // priority
        NULL                               // handle
    );
}

void loop() {
    // nothing here, everything runs in FreeRTOS tasks
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}