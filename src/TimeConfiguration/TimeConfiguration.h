// TimeUtility.h
#define TINY_GSM_MODEM_SIM7600
#pragma once

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <time.h>

// Converts a UTC broken-down time into Unix timestamp.
// Works on Arduino (no timezone, no DST problems)
static time_t timegm_arduino(const struct tm *tm)
{
    const int YEAR = tm->tm_year + 1900;
    const int MONTH = tm->tm_mon + 1;

    // Days before month
    static const int daysBeforeMonth[] =
        {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

    // Count days since epoch
    int yearsSince1970 = YEAR - 1970;

    // Count leap years since 1970
    int leapDays =
        (yearsSince1970 + 2) / 4 -
        (yearsSince1970 + 70) / 100 +
        (yearsSince1970 + 370) / 400;

    // Total days
    long days =
        yearsSince1970 * 365L +
        leapDays +
        daysBeforeMonth[tm->tm_mon] +
        (tm->tm_mday - 1);

    // If current year is leap and month > Feb, add 1 day
    if ((MONTH > 2) &&
        ((YEAR % 4 == 0 && YEAR % 100 != 0) || (YEAR % 400 == 0)))
    {
        days += 1;
    }

    // build timestamp
    return days * 86400L +
           tm->tm_hour * 3600L +
           tm->tm_min * 60L +
           tm->tm_sec;
}

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
    int64_t baseMillis = 0;
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
        Serial.println((long)baseUnixMs);
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

        // ---------------------------
        // Build tm struct (UTC)
        // ---------------------------
        tm timeinfo{};
        timeinfo.tm_year = y - 1900;
        timeinfo.tm_mon = mo - 1;
        timeinfo.tm_mday = d;
        timeinfo.tm_hour = h;
        timeinfo.tm_min = mi;
        timeinfo.tm_sec = s;

        // Extract offset "+28" → +2.8 hours = UTC+2h48m (INSANE)
        // SIM7600 uses *quarters of an hour* (15 minutes)
        //
        // "+28" = +2.0 hours + 8 * 15min = +2h + 2h = *+4 HOURS*
        //
        int tz_quarters = datetime.substring(18).toInt(); // "28"
        int tz_offset_seconds = tz_quarters * 15 * 60;

        // Convert local → UTC
        time_t unixLocal = timegm_arduino(&timeinfo);
        time_t unixUtc = unixLocal - tz_offset_seconds;

        Serial.print("Unix seconds (UTC): ");
        Serial.println((long)unixUtc);

        return (int64_t)unixUtc * 1000LL;
    }
};
