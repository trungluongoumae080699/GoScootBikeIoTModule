#include <Wire.h>
#include <U8g2lib.h>
#include <qrcode.h>
#include <UI/DisplayTask.h>
#include <UI/App_logo.h>

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(
    U8G2_R0,
    U8X8_PIN_NONE // reset pin not used
);

int batteryLevel = 75;        // example battery level
float currentSpeedKmh = 15.5; // example speed
bool toBeUpdated = true;
DisplayPage currentPage = DisplayPage::LowBatteryAlert;
DisplayTask displayTask(
    currentSpeedKmh,
    batteryLevel,
    currentPage,
    toBeUpdated,
    app_logo_bitmap,         // no default bitmap
    "Something is wrong...." // example QR text
);

void setup()
{
    Serial.begin(115200);
    Serial.println("Starting.....");
    Wire.begin();       // Mega I2C pins
    Serial.println("Wiring done..");
    u8g2.begin();       // REQUIRED
    Serial.println("u8g2 done...");
    toBeUpdated = true; // force first draw
    Serial.println("Setup done");

}

void loop()
{
    displayTask.display();
}