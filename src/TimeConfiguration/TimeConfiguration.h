// TimeUtility.h
#define TINY_GSM_MODEM_SIM7600
#pragma once

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <time.h>

/**
 * TimeUtility
 *
 * - Dùng TinyGsm modem để lấy thời gian 1 lần bằng lệnh AT+CCLK?
 * - Sau đó giữ một "mốc" (baseUnixMs, baseMillis) và dùng millis()
 *   để suy ra thời gian hiện tại mà không cần hỏi modem nữa.
 *
 *  Flow dùng:
 *
 *    TimeUtility timeUtil(gsm.modem);
 *    void setup() {
 *        ...
 *        if (!timeUtil.syncOnceBlocking()) {
 *            Serial.println(F("Time sync failed"));
 *        }
 *    }
 *
 *    void loop() {
 *        int64_t nowMs = timeUtil.nowUnixMs();
 *        ...
 *    }
 */
struct TimeConfiguration
{
    TinyGsm &modem;

    // mốc thời gian từ modem (ms since epoch)
    int64_t baseUnixMs = -1;
    // millis() tại thời điểm sync
    uint32_t baseMillis = 0;
    bool valid = false;

    explicit TimeConfiguration(TinyGsm &m)
        : modem(m)
    {
    }

    // -------------------------------------------------
    //  Gọi 1 lần trong setup: hỏi modem +CCLK?
    //  Trả về true nếu sync thành công.
    // -------------------------------------------------
    bool syncOnceBlocking(uint32_t timeoutMs = 2000)
    {
        int64_t ts = getUnixTimestampFromModem(timeoutMs);
        if (ts < 0)
        {
            valid = false;
            return false;
        }

        baseUnixMs = ts;
        baseMillis = millis();
        valid = true;

        Serial.print(F("[TIME] synced unix ms = "));
        Serial.println((long)baseUnixMs);
        return true;
    }

    // -------------------------------------------------
    //  Lấy thời gian hiện tại (ms since epoch)
    //  Dựa vào mốc baseUnixMs + (millis() - baseMillis)
    // -------------------------------------------------
    int64_t nowUnixMs() const
    {
        if (!valid || baseUnixMs < 0)
            return -1;

        uint32_t elapsed = millis() - baseMillis; // wrap-safe
        return baseUnixMs + (int64_t)elapsed;
    }

    // Thời gian hiện tại tính theo giây (epoch seconds)
    int64_t nowUnixSeconds() const
    {
        int64_t ms = nowUnixMs();
        if (ms < 0)
            return -1;
        return ms / 1000LL;
    }

    bool hasValidTime() const { return valid; }

private:
    // Hàm gốc của em, chỉ dùng nội bộ để lấy mốc ban đầu
    int64_t getUnixTimestampFromModem(uint32_t timeoutMs)
    {
        modem.sendAT("+CCLK?");

        uint32_t start = millis();
        String line;
        String cclkLine;

        Serial.println(F("---- Reading CCLK response ----"));

        while (millis() - start < timeoutMs)
        {
            if (modem.stream.available())
            {
                line = modem.stream.readStringUntil('\n');
                line.trim();
                if (line.length() == 0)
                    continue;

                Serial.print(F("LINE: "));
                Serial.println(line);

                if (line.startsWith("+CCLK"))
                {
                    cclkLine = line;
                }
                if (line == "OK")
                    break;
                if (line.indexOf("ERROR") >= 0)
                {
                    Serial.println(F("CCLK returned ERROR"));
                    return -1;
                }
            }
        }

        if (cclkLine.length() == 0)
        {
            Serial.println(F("CCLK not found within timeout"));
            return -1;
        }

        int quote1 = cclkLine.indexOf('"');
        int quote2 = cclkLine.indexOf('"', quote1 + 1);
        if (quote1 < 0 || quote2 < 0)
        {
            Serial.println(F("CCLK line format invalid"));
            return -1;
        }

        String datetime = cclkLine.substring(quote1 + 1, quote2);
        int y = datetime.substring(0, 2).toInt() + 2000;
        int mo = datetime.substring(3, 5).toInt();
        int d = datetime.substring(6, 8).toInt();
        int h = datetime.substring(9, 11).toInt();
        int mi = datetime.substring(12, 14).toInt();
        int s = datetime.substring(15, 17).toInt();

        tm timeinfo{};
        timeinfo.tm_year = y - 1900;
        timeinfo.tm_mon = mo - 1;
        timeinfo.tm_mday = d;
        timeinfo.tm_hour = h;
        timeinfo.tm_min = mi;
        timeinfo.tm_sec = s;

        time_t t = mktime(&timeinfo);
        Serial.print(F("Unix seconds: "));
        Serial.println((long)t);

        return (int64_t)t * 1000LL;
    }
};