#pragma once

#include <Arduino.h>
#include "NetworkTask.h"
#include "NetworkConfiguration/HttpConfiguration.h"
#include "Domains/CellInfo.h"

class QueryGeolocationApiTask : public NetworkTask
{
private:
    HttpConfiguration &http;
    CellInfo &cellPtr;   // pointer to parsed CellInfo (from CPSI)
    float &latPtr;       // pointer to output latitude
    float &lonPtr;       // pointer to output longitude
    uint32_t perRequestTimeoutMs = 2000;   // e.g. 2s timeout

public:
    QueryGeolocationApiTask(HttpConfiguration &httpCfg,
                            CellInfo &cellInfo,
                            float &outLat,
                            float &outLon)
        : http(httpCfg),
          cellPtr(cellInfo),
          latPtr(outLat),
          lonPtr(outLon)
    {
    }

    // optional
    bool isMandatory() const override { return false; }

    void execute() override
    {
        if (isCompleted())
            return;

        

        // -------- 1) First call: build body + start HTTP --------
        if (!isStarted())
        {


            String body = cellPtr.buildLocationApiJson();
            if (body.length() == 0)
            {
                Serial.println(F("[GEO] buildLocationApiJson() returned empty body"));
                markCompleted();
                return;
            }

            // HTTP layer must be idle
            if (!http.isIdle())
            {
                Serial.println(F("[GEO] HTTP busy, will retry start next tick"));
                return;  // wait for a later loop
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
                markCompleted();
                return;
            }

            markStarted();
            return; // first call only starts request
        }

        // -------- 2) After started: advance HTTP state machine --------
        http.stepHttp();

        if (!http.isHttpDone())
        {
            // not finished yet → wait next loop
            return;
        }

        // -------- 3) HTTP finished (OK or ERROR) --------
        String resp = http.getHttpResult();
        bool ok     = http.isHttpOk();

        http.resetHttp();   // free HTTP for other tasks

        if (!ok || resp.length() == 0)
        {
            Serial.println(F("[GEO] HTTP error or empty response from Unwired Labs"));
            markCompleted();
            return;
        }

        // 4) Strip HTTP headers
        int headerEnd   = resp.indexOf("\r\n\r\n");
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
                    markCompleted();
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
            markCompleted();
            return;
        }

        int latColon = jsonPart.indexOf(':', latIndex);
        int lonColon = jsonPart.indexOf(':', lonIndex);
        if (latColon < 0 || lonColon < 0)
        {
            Serial.println(F("[GEO] Malformed lat/lon fields"));
            markCompleted();
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

        if (latPtr) latPtr = latVal;
        if (lonPtr) lonPtr = lonVal;

        Serial.print(F("[GEO] Parsed lat = "));
        Serial.println(latVal, 6);
        Serial.print(F("[GEO] Parsed lon = "));
        Serial.println(lonVal, 6);

        markCompleted();   // one-shot, done
    }

protected:
    void markStarted() override
    {
        NetworkTask::markStarted();
        // optional extra logging:
        // Serial.println(F("[GEO] QueryGeolocationApiTask started"));
    }

    void markCompleted() override
    {
        Serial.println(F("[GEO] QueryGeolocationApiTask completed"));
        cellPtr.isOutdated = true;
        NetworkTask::markCompleted();
    }
};