#pragma once

#include <Arduino.h>
#include <U8g2lib.h>
#include <qrcode.h>

extern U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2;

// ---------- Display pages ----------
enum class DisplayPage
{
    QrScan,
    Welcome,
    LowBatteryAlert,
    BoundaryCrossAlert,
    PleaseWait,
    IncorrectQrScan,
    GenericAlert,
};

inline bool isAlertPage(DisplayPage p)
{
    return p == DisplayPage::GenericAlert ||
           p == DisplayPage::IncorrectQrScan;
}

// ---------- DisplayTask ----------
struct DisplayTask
{
    // live data refs
    float &speedKmh;
    int &batteryPercent;
    DisplayPage &currentPage;
    bool &toBeUpdated;

    // static assets
    const unsigned char *defaultBitmap; // bitmap for non-QR screens (e.g. 64x64 logo)
    const char *qrContent;              // QR text for scan screen

    // alert + page timing
    unsigned long lastAlertStartMs = 0;
    unsigned long lastPageSwitchMs = 0;
    const unsigned long alertDurationMs = 4000;     // 4 seconds
    const unsigned long normalPageTimeoutMs = 5000; // 5 seconds
    DisplayPage lastNonAlertPage = DisplayPage::QrScan;

    // text buffer
    String text;

    DisplayTask(float &speedRef,
                int &batteryRef,
                DisplayPage &pageRef,
                bool &updateFlagRef,
                const unsigned char *bitmap,
                const char *qrText)
        : speedKmh(speedRef),
          batteryPercent(batteryRef),
          currentPage(pageRef),
          toBeUpdated(updateFlagRef),
          defaultBitmap(bitmap),
          qrContent(qrText) {}

    void display()
    {
        unsigned long now = millis();

        // ----- 4s timeout for alert pages -----
        if (isAlertPage(currentPage))
        {
            if (lastAlertStartMs == 0)
            {
                lastAlertStartMs = now;
            }
            else if (now - lastAlertStartMs >= alertDurationMs)
            {
                currentPage = DisplayPage::Welcome;
                toBeUpdated = true;
                lastAlertStartMs = 0;
            }
        }

        // nothing to redraw
        if (!toBeUpdated)
        {
            return;
        }

        // decide QR / bitmap + text by page
        bool useQr = false;
        const unsigned char *img = defaultBitmap;
        const char *qrText = nullptr;

        switch (currentPage)
        {
        case DisplayPage::QrScan:
            text = F("Scan to start riding");
            useQr = true;
            qrText = qrContent;
            break;

        case DisplayPage::Welcome:
            text = F("Have a safe ride!");
            useQr = false;
            qrText = nullptr;
            break;

        case DisplayPage::LowBatteryAlert:
            text = F("Battery low!");
            useQr = false;
            qrText = nullptr;
            break;

        case DisplayPage::BoundaryCrossAlert:
            text = F("You are leaving the allowed area!");
            useQr = false;
            qrText = nullptr;
            break;
        case DisplayPage::PleaseWait:
            text = F("Please wait...");
            useQr = false;
            qrText = nullptr;
            break;

        case DisplayPage::GenericAlert:
            text = F("Something is wrong...");
            useQr = false;
            qrText = nullptr;
            break;

        case DisplayPage::IncorrectQrScan:
            text = F("Incorrect QR");
            useQr = false;
            qrText = nullptr;
            break;
        }

        // main draw call
        drawDisplayScreen(img, useQr, qrText, text.c_str());

        // reset update flag
        toBeUpdated = false;
    }

private:
    // ---------- MAIN LAYOUT: image/QR + text + speed + battery ----------
    void drawDisplayScreen(const unsigned char *bitmap,
                           bool useQr,
                           const char *qrText,
                           const char *textStr)
    {
        const int screenW = 128;
        const int screenH = 64;

        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tf);
        int lineHeight = u8g2.getMaxCharHeight();

        // left side: image / QR occupies roughly half screen
        const int leftMargin = 2;
        const int textGapX = 4;
        int leftWidth = 0;
        int imgX = leftMargin;
        int imgY = 0;

        if (useQr && qrText)
        {
            // --- draw QR on the left ---
            QRCode qrcode;
            uint8_t qrcodeData[200];

            qrcode_initText(&qrcode, qrcodeData, 1, ECC_MEDIUM, qrText);

            const uint8_t moduleSize = 3;
            const uint16_t qrPixelSize = qrcode.size * moduleSize;

            leftWidth = qrPixelSize;
            imgY = (screenH - qrPixelSize) / 2;

            for (uint8_t y = 0; y < qrcode.size; y++)
            {
                for (uint8_t x = 0; x < qrcode.size; x++)
                {
                    if (qrcode_getModule(&qrcode, x, y))
                    {
                        u8g2.drawBox(
                            imgX + x * moduleSize,
                            imgY + y * moduleSize,
                            moduleSize, moduleSize);
                    }
                }
            }
        }
        else if (bitmap)
        {
            // --- draw bitmap logo on the left ---
            const int bmpW = 64; // assuming 64x64 bitmap
            const int bmpH = 64;
            leftWidth = bmpW;
            imgY = (screenH - bmpH) / 2;
            u8g2.drawXBM(imgX, imgY, bmpW, bmpH, bitmap);
        }

        // right side: text region
        const int textX = imgX + leftWidth + textGapX;
        const int textWidth = screenW - textX - 2;

        // 1) Draw wrapped text
        int nextY = drawWrappedText(textX, 10, textWidth, textStr);

        int textLastBaselineY = nextY - lineHeight;

        // 2) Battery at bottom-right of text area
        const int batteryWidth = 20;
        const int batteryHeight = 8;
        const int bottomMargin = 2;
        const int batteryGapX = 4;

        int batteryY = screenH - bottomMargin - batteryHeight;
        int batteryX = textX + batteryGapX;

        drawBattery(batteryX, batteryY, batteryWidth, batteryHeight, batteryPercent);

        char buf[32];
        snprintf(buf, sizeof(buf), "%d%%", batteryPercent);
        int batteryTextBaselineY = batteryY + batteryHeight - 1;
        u8g2.drawStr(batteryX + batteryWidth + 4, batteryTextBaselineY, buf);

        // 3) Speed centered vertically between last text and battery
        int regionTopY = textLastBaselineY;
        int regionBottomY = batteryY;
        int speedBaselineY = regionTopY + (regionBottomY - regionTopY) / 2;

        snprintf(buf, sizeof(buf), "%.1f km/h", speedKmh);
        int speedWidth = u8g2.getStrWidth(buf);
        int speedX = textX + (textWidth - speedWidth) / 2;

        u8g2.drawStr(speedX, speedBaselineY, buf);

        u8g2.sendBuffer();
    }

    // ---------- Battery icon ----------
    void drawBattery(int x, int y, int width, int height, int percentage)
    {
        if (percentage < 0)
            percentage = 0;
        if (percentage > 100)
            percentage = 100;

        int border = 1;
        int termWidth = width / 6;
        int termHeight = height / 3;

        int termY = y + (height - termHeight) / 2;

        u8g2.drawFrame(x, y, width, height);
        u8g2.drawBox(x + width, termY, termWidth, termHeight);

        int innerWidth = width - termWidth - border * 2;
        int fillWidth = (innerWidth * percentage) / 100;

        if (fillWidth > 0)
        {
            u8g2.drawBox(
                x + border,
                y + border,
                fillWidth,
                height - border * 2);
        }
    }

    // ---------- Wrapped text ----------
    int drawWrappedText(int x, int y, int maxWidth, const char *text)
    {
        if (!text || !*text)
            return y;

        char buffer[120];
        strncpy(buffer, text, sizeof(buffer));
        buffer[sizeof(buffer) - 1] = 0;

        char *word;
        String currentLine = "";

        word = strtok(buffer, " ");
        while (word != NULL)
        {
            String testLine = currentLine + word + " ";

            if (u8g2.getStrWidth(testLine.c_str()) > maxWidth)
            {
                u8g2.drawStr(x, y, currentLine.c_str());
                y += u8g2.getMaxCharHeight();
                currentLine = (String)word + " ";
            }
            else
            {
                currentLine = testLine;
            }

            word = strtok(NULL, " ");
        }

        if (currentLine.length() > 0)
        {
            u8g2.drawStr(x, y, currentLine.c_str());
            y += u8g2.getMaxCharHeight();
        }

        return y;
    }
};