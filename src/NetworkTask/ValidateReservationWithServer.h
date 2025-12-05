#pragma once

#include <Arduino.h>
#include "NetworkTask.h"
#include "NetworkConfiguration/HttpConfiguration.h"
#include "Domains/Bike.h"

class ValidateTripWithServerTask : public NetworkTask
{
private:
    HttpConfiguration &http;
    const char *url;            // endpoint, e.g. "http://your-backend/trip/validate"
    String requestBody;         // JSON gửi lên server (QR JSON)
    String *tripIdPtr;          // con trỏ tới biến tripId bên ngoài (update nếu isValid)
    BikeState *bikeState;       // trạng thái xe (IDLE / INUSED / ...)

    uint32_t perRequestTimeoutMs = 2000;   // ví dụ: 2s timeout cho validate trip

public:
    /**
     * @param httpCfg      HttpConfiguration dùng TinyGsmClient
     * @param endpointUrl  URL của API validate trip
     * @param bodyJson     JSON body (thường là QR JSON client gửi)
     * @param tripIdOut    con trỏ tới biến tripId hiện tại (sẽ update nếu isValid == true)
     * @param bikeStatePtr con trỏ tới biến BikeState bên ngoài
     */
    ValidateTripWithServerTask(HttpConfiguration &httpCfg,
                               const char *endpointUrl,
                               const String &bodyJson,
                               String *tripIdOut,
                               BikeState *bikeStatePtr)
        : http(httpCfg),
          url(endpointUrl),
          requestBody(bodyJson),
          tripIdPtr(tripIdOut),
          bikeState(bikeStatePtr)
    {
    }

    // Trip validation should NOT be dropped if possible
    bool isMandatory() const override { return true; }

    // Task “tick” – gọi lặp lại trong loop() / scheduler
    void execute() override
    {
        if (isCompleted())
            return;

        // 1) Lần đầu: start HTTP POST (non-blocking)
        if (!isStarted())
        {
            Serial.println(F("[TRIP] ValidateTripWithServerTask: starting HTTP POST"));

            if (!http.isIdle())
            {
                // HTTP layer đang bận request khác → đợi vòng sau
                Serial.println(F("[TRIP] HTTP busy, will retry start"));
                return;
            }

            bool ok = http.startHttpPostJson(url, requestBody, perRequestTimeoutMs);
            if (!ok)
            {
                Serial.println(F("[TRIP] Failed to start HTTP POST"));
                markCompleted();   // one-shot, mark done (fail)
                return;
            }

            markStarted();
            return; // lần này chỉ mới gửi, chưa có gì để đọc
        }

        // 2) Sau khi đã start: tiến HTTP state machine
        http.stepHttp();

        if (!http.isHttpDone())
        {
            // chưa xong (đang đọc hoặc chờ / chưa timeout)
            return;
        }

        // 3) HTTP đã xong (OK hoặc ERROR)
        String resp = http.getHttpResult();
        bool ok     = http.isHttpOk();

        http.resetHttp();   // giải phóng HTTP layer cho task khác

        if (!ok || resp.length() == 0)
        {
            Serial.println(F("[TRIP] HTTP error or empty response"));
            markCompleted();
            return;
        }

        // 4) Tách header khỏi body
        int headerEnd   = resp.indexOf("\r\n\r\n");
        String jsonPart = (headerEnd >= 0) ? resp.substring(headerEnd + 4) : resp;

        Serial.println(F("[TRIP] Raw JSON from server:"));
        Serial.println(jsonPart);

        // Giả định JSON:
        //   { "isValid": true, "id": "trip-id-or-reason" }

        // --- parse isValid ---
        bool parsedIsValid = false;
        bool isValidValue  = false;

        int isValidIdx = jsonPart.indexOf("\"isValid\"");
        if (isValidIdx >= 0)
        {
            int colon = jsonPart.indexOf(':', isValidIdx);
            if (colon > 0)
            {
                String boolStr = jsonPart.substring(colon + 1);
                boolStr.trim();

                int comma = boolStr.indexOf(',');
                if (comma >= 0)
                    boolStr = boolStr.substring(0, comma);
                int brace = boolStr.indexOf('}');
                if (brace >= 0)
                    boolStr = boolStr.substring(0, brace);

                boolStr.trim();
                boolStr.toLowerCase();

                if (boolStr.startsWith("true"))
                {
                    parsedIsValid = true;
                    isValidValue  = true;
                }
                else if (boolStr.startsWith("false"))
                {
                    parsedIsValid = true;
                    isValidValue  = false;
                }
            }
        }

        // --- parse id (string) ---
        String idValue = "";
        int idIdx = jsonPart.indexOf("\"id\"");
        if (idIdx >= 0)
        {
            int colon = jsonPart.indexOf(':', idIdx);
            if (colon > 0)
            {
                int firstQuote = jsonPart.indexOf('"', colon + 1);
                if (firstQuote >= 0)
                {
                    int secondQuote = jsonPart.indexOf('"', firstQuote + 1);
                    if (secondQuote > firstQuote)
                    {
                        idValue = jsonPart.substring(firstQuote + 1, secondQuote);
                    }
                }
            }
        }

        if (!parsedIsValid)
        {
            Serial.println(F("[TRIP] Could not parse isValid field"));
            if (idValue.length() > 0)
            {
                Serial.print(F("[TRIP] Server message: "));
                Serial.println(idValue);
            }
            else
            {
                Serial.println(F("[TRIP] Invalid response: missing isValid"));
            }
            markCompleted();
            return;
        }

        Serial.print(F("[TRIP] isValid = "));
        Serial.println(isValidValue ? F("true") : F("false"));
        Serial.print(F("[TRIP] id / reason = "));
        Serial.println(idValue);

        // 5) Nếu hợp lệ, cập nhật tripId + bikeState
        if (isValidValue && tripIdPtr != nullptr && idValue.length() > 0)
        {
            *tripIdPtr = idValue;
            Serial.print(F("[TRIP] tripId updated to: "));
            Serial.println(*tripIdPtr);

            if (bikeState != nullptr)
            {
                *bikeState = INUSED;  // hoặc enum cụ thể của bạn
                Serial.println(F("[TRIP] bikeState -> INUSED"));
            }
        }

        markCompleted();  // one-shot task done
    }
};