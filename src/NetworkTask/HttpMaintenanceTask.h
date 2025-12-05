#pragma once

#include <Arduino.h>
#include "NetworkTask/NetworkTask.h"
#include "NetworkConfiguration/HttpConfiguration.h"

class HttpMaintenanceTask : public NetworkTask
{
public:
    explicit HttpMaintenanceTask(HttpConfiguration &httpRef)
        : http(httpRef)
    {
    }

    // HTTP pump là non-mandatory
    bool isMandatory() const override { return false; }

    void execute() override
    {
        if (isCompleted())
            return;

        if (!isStarted()) {
            markStarted();
            // Serial.println(F("[TASK] HttpMaintenanceTask started"));
        }

        // Bơm đúng 1 tick dữ liệu cho HTTP client
        http.stepHttp();

        // One-shot maintenance tick → xong thì completed
        markCompleted();
        // Serial.println(F("[TASK] HttpMaintenanceTask completed"));
    }

private:
    HttpConfiguration &http;
};