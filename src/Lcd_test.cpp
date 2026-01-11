#include <Wire.h>
#include <U8g2lib.h>

// ================= OLED CONFIG =================
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(
    U8G2_R0,
    U8X8_PIN_NONE // reset pin not used
);

// ================= TEST DATA =================
int batteryLevel = 75;          // fake battery %
float currentSpeedKmh = 15.5;   // fake speed

// ================= SETUP =================
void setup()
{
    Serial.begin(115200);
    delay(200);

    Serial.println("Starting...");
    
    // I2C init (Mega: SDA 20, SCL 21 | ESP32 default OK)
    Wire.begin();
    Serial.println("I2C initialized");

    // OLED init
    u8g2.begin();
    Serial.println("U8G2 initialized");

    Serial.println("Setup done");
}

// ================= LOOP =================
void loop()
{
    u8g2.clearBuffer();

    // ---- Title ----
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.drawStr(0, 12, "GoScoot Display OK");

    // ---- Battery ----
    char buf[32];
    sprintf(buf, "Battery: %d%%", batteryLevel);
    u8g2.drawStr(0, 28, buf);

    // ---- Speed ----
    sprintf(buf, "Speed: %.1f km/h", currentSpeedKmh);
    u8g2.drawStr(0, 44, buf);

    // ---- Battery Bar ----
    int barWidth = map(batteryLevel, 0, 100, 0, 100);
    u8g2.drawFrame(0, 50, 128, 10);
    u8g2.drawBox(2, 52, barWidth, 6);

    u8g2.sendBuffer();

    // Fake animation
    batteryLevel--;
    if (batteryLevel < 0) batteryLevel = 100;

    delay(500);
}