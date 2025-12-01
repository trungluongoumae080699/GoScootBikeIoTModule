#include <Arduino.h>
#include <Trip.h>
#include <WiFi.h>

#include <Arduino.h>
#include "CameraUtility.h"
#include <ESP32QRCodeReader.h>

// dùng AI Thinker pin map – trùng với camera_config bạn đang dùng
ESP32QRCodeReader qr(CAMERA_MODEL_AI_THINKER);

// struct chứa dữ liệu QR do lib cung cấp
QRCodeData qrCodeData;

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("=== ESP32-CAM QR Scanner (ESP32QRCodeReader) ===");

    // KHÔNG gọi initCamera() nữa – thư viện tự init camera bên trong
    qr.setup();
    Serial.println("Setup QRCode Reader");

    // chạy xử lý camera trên core 1 cho mượt hơn
    qr.beginOnCore(1);
    Serial.println("Begin QR task on core 1");
}


void loop() {
    // receiveQrCode(&qrCodeData, timeout_ms)
    if (qr.receiveQrCode(&qrCodeData, 100)) {
        Serial.println("Scanned new QRCode");

        if (qrCodeData.valid) {
            Serial.print("Valid payload: ");
            Serial.println((const char *)qrCodeData.payload);
        } else {
            Serial.print("Invalid payload: ");
            Serial.println((const char *)qrCodeData.payload);
        }

        // TODO: chỗ này bạn xử lý QR:
        //  - parse trip/session/bike id
        //  - gửi HTTP request
        //  - đổi state UI, bật relay, v.v.
        delay(1500);  // tránh spam nếu giữ QR trước camera
    }

    delay(50);
}