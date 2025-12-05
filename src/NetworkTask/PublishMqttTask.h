#pragma once

#include <Arduino.h>
#include "NetworkTask.h"
#include "NetworkConfiguration/GsmConfiguration.h"

// Non-mandatory task: publish a binary payload via MQTT
struct PublishMqttTask : public NetworkTask
{
    GsmConfiguration &gsm;
    uint8_t *data;       // owned copy of payload
    size_t length;       // payload length
    const char *topic;   // MQTT topic (not owned)

    PublishMqttTask(GsmConfiguration &gsmRef,
                    const uint8_t *payload,
                    size_t payloadLen,
                    const char *mqttTopic)
        : gsm(gsmRef),
          data(nullptr),
          length(payloadLen),
          topic(mqttTopic)
    {
        if (payload && payloadLen > 0)
        {
            data = new uint8_t[payloadLen];
            memcpy(data, payload, payloadLen);
        }
    }

    ~PublishMqttTask() override
    {
        if (data)
        {
            delete[] data;
            data = nullptr;
        }
    }

    void execute() override
    {
        if (isCompleted())
            return; // already done

        if (!isStarted())
        {
            markStarted();
        }

        if (!topic)
        {
            Serial.println(F("[TASK] PublishMqttTask: No topic"));
            markCompleted();
            return;
        }

        if (!data || length == 0)
        {
            Serial.println(F("[TASK] PublishMqttTask: Empty payload"));
            markCompleted();
            return;
        }

        // One-shot MQTT publish using binary buffer
        bool ok = gsm.publishMqtt(data, length, topic);
        if (!ok)
        {
            Serial.println(F("[TASK] publishTelemetry (binary) FAILED"));
        }
        else
        {
            Serial.print(F("[TASK] publishTelemetry OK, len="));
            Serial.println(length);
        }

        markCompleted();
    }

    // Telemetry is skippable → NOT mandatory
    bool isMandatory() const override
    {
        return false;
    }

protected:
    void markStarted() override
    {
        Serial.println(F("[TASK] PublishMqttTask started"));
        NetworkTask::markStarted();
    }

    void markCompleted() override
    {
        Serial.println(F("[TASK] PublishMqttTask completed"));
        NetworkTask::markCompleted();
    }
};