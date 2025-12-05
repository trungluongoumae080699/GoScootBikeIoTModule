#pragma once

#include <Arduino.h>
#include "NetworkTask/NetworkTask.h"
#include "NetworkConfiguration/GsmConfiguration.h"

class MqttMaintenanceTask : public NetworkTask
{
public:
    explicit MqttMaintenanceTask(GsmConfiguration &gsmRef)
        : gsm(gsmRef)
    {
    }

    // MQTT keep-alive là non-mandatory
    bool isMandatory() const override { return false; }

    void execute() override
    {
        if (isCompleted())
            return;

        if (!isStarted()) {
            markStarted();
            // Serial.println(F("[TASK] MqttMaintenanceTask started"));
        }

        // One non-blocking tick for MQTT keep-alive
        gsm.stepMqtt();

        // Task is one-shot → mark as done immediately
        markCompleted();
        // Serial.println(F("[TASK] MqttMaintenanceTask completed"));
    }

private:
    GsmConfiguration &gsm;
};