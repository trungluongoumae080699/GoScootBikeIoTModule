#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// Mega pins
#define TFT_CS   10
#define TFT_DC    8
#define TFT_RST   9

// Hardware SPI uses MOSI=51, SCK=52 on Mega
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("ST7789 Test");

    tft.init(240, 320);   // resolution for your TFT
    Serial.println("Initialized");
    tft.fillScreen(ST77XX_BLACK);

    tft.setCursor(10, 10);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_GREEN);
    Serial.println("ST7789 OK!");

    tft.drawLine(0, 0, 239, 319, ST77XX_RED);
}

void loop() {
}