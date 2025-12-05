#pragma once

#include <Arduino.h>
#include "NetworkTask.h"
#include "NetworkConfiguration/HttpConfiguration.h"
#include "Domains/CellInfo.h"

class QueryGeolocationApiTask : public NetworkTask
{
private:
    HttpConfiguration &http;
    CellInfo *cellPtr;   // pointer to parsed CellInfo (from CPSI)
    float *latPtr;       // pointer to output latitude
    float *lonPtr;       // pointer to output longitude
    bool started   = false;
    bool completed = false;
    uint32_t perRequestTimeoutMs = 2000;   // ví dụ: 2s timeout

public:
    QueryGeolocationApiTask(HttpConfiguration &httpCfg,
                            CellInfo *cellInfo,
                            float *outLat,
                            float *outLon)
        : http(httpCfg),
          cellPtr(cellInfo),
          latPtr(outLat),
          lonPtr(outLon)
    {
    }

    virtual void execute() override
    {
        if (completed) return;

        // -------- 1) First call: build body + start HTTP --------
        if (!started)
        {
            if (!cellPtr || !latPtr || !lonPtr)
            {
                Serial.println(F("[GEO] Null pointer(s) in QueryGeolocationApiTask"));
                completed = true;
                return;
            }

            String body = cellPtr->buildLocationApiJson();
            if (body.length() == 0)
            {
                Serial.println(F("[GEO] buildLocationApiJson() returned empty body"));
                completed = true;
                return;
            }

            // HTTP layer phải rảnh
            if (!http.isIdle())
            {
                Serial.println(F("[GEO] HTTP busy, will retry start next tick"));
                return;  // chờ vòng loop sau
            }

            Serial.println(F("[GEO] Sending HTTP POST to Unwired Labs (non-blocking)..."));

            bool ok = http.startHttpPostJson(
                "http://eu1.unwiredlabs.com/v2/process.php",
                body,
                perRequestTimeoutMs
            );

            if (!ok)
            {
                Serial.println(F("[GEO] Failed to start HTTP POST"));
                completed = true;
                return;
            }

            started = true;
            return; // lần đầu chỉ mới gửi request
        }

        // -------- 2) After started: advance HTTP state machine --------
        http.stepHttp();

        if (!http.isHttpDone())
        {
            // chưa nhận xong / chưa timeout → chờ vòng loop sau
            return;
        }

        // -------- 3) HTTP finished (OK or ERROR) --------
        cellPtr = nullptr;
        String resp = http.getHttpResult();
        bool ok     = http.isHttpOk();

        http.resetHttp();   // giải phóng cho task khác

        if (!ok || resp.length() == 0)
        {
            Serial.println(F("[GEO] HTTP error or empty response from Unwired Labs"));
            completed = true;
            return;
        }

        // 4) Strip HTTP headers
        int headerEnd  = resp.indexOf("\r\n\r\n");
        String jsonPart = (headerEnd >= 0) ? resp.substring(headerEnd + 4) : resp;

        Serial.println(F("[GEO] Raw JSON from server:"));
        Serial.println(jsonPart);

        // Optional: status:"ok"
        int statusIdx = jsonPart.indexOf("\"status\"");
        if (statusIdx >= 0)
        {
            int colon = jsonPart.indexOf(':', statusIdx);
            if (colon > 0)
            {
                String statusVal = jsonPart.substring(colon + 1);
                statusVal.trim();
                if (statusVal.indexOf("ok") < 0 && statusVal.indexOf("OK") < 0)
                {
                    Serial.println(F("[GEO] status is not ok"));
                    completed = true;
                    return;
                }
            }
        }

        // 5) Extract "lat" and "lon"
        int latIndex = jsonPart.indexOf("\"lat\"");
        int lonIndex = jsonPart.indexOf("\"lon\"");

        if (latIndex < 0 || lonIndex < 0)
        {
            Serial.println(F("[GEO] Missing lat/lon fields in JSON"));
            completed = true;
            return;
        }

        int latColon = jsonPart.indexOf(':', latIndex);
        int lonColon = jsonPart.indexOf(':', lonIndex);
        if (latColon < 0 || lonColon < 0)
        {
            Serial.println(F("[GEO] Malformed lat/lon fields"));
            completed = true;
            return;
        }

        String latStr = jsonPart.substring(latColon + 1);
        String lonStr = jsonPart.substring(lonColon + 1);
        latStr.trim();
        lonStr.trim();

        float latVal = latStr.toFloat();
        float lonVal = lonStr.toFloat();

        if (latVal == 0 && lonVal == 0)
        {
            Serial.println(F("[GEO] Warning: parsed (0,0) – check JSON / API key"));
        }

        *latPtr = latVal;
        *lonPtr = lonVal;

        Serial.print(F("[GEO] Parsed lat = "));
        Serial.println(latVal, 6);
        Serial.print(F("[GEO] Parsed lon = "));
        Serial.println(lonVal, 6);

        completed = true;   // one-shot, done
    }

    bool isCompleted() const
    {
        return completed;
    }
};