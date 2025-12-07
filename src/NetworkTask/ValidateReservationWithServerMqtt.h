#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "NetworkTask.h"
#include "NetworkConfiguration/GsmConfiguration.h"
#include "Domains/Bike.h"
#include "Domains/Trip.h"

// Forward declaration so global callback can see it
class ValidateTripWithServerTaskMqtt;
extern ValidateTripWithServerTaskMqtt *g_activeValidationTask;

class ValidateTripWithServerTaskMqtt : public NetworkTask
{
private:
    GsmConfiguration &gsm;
    Trip trip;                 // trip info we send to server
    const char *requestTopic;  // e.g. "/reservation/validation/BIK_298A1J35"
    const char *responseTopic; // e.g. "/reservation/BIK_298A1J35/validation/response"
    String &tripIdRef;
    BikeState &bikeStateRef;
    String &lcdLine1;
    String &lcdLine2;

    bool awaitingResponse = false;
    bool responseReceived = false;
    bool validationSuccess = false;
    String serverIdOrMsg;

    uint32_t overallTimeoutMs = 15000; // 15s for full request+response

public:
    ValidateTripWithServerTaskMqtt(GsmConfiguration &gsmRef,
                                   const Trip &tripIn,
                                   const char *requestTopicIn,
                                   const char *responseTopicIn,
                                   String &tripIdOut,
                                   BikeState &bikeStateOut,
                                   String &line1,
                                   String &line2)
        : gsm(gsmRef),
          trip(tripIn),
          requestTopic(requestTopicIn),
          responseTopic(responseTopicIn),
          tripIdRef(tripIdOut),
          bikeStateRef(bikeStateOut),
          lcdLine1(line1),
          lcdLine2(line2)
    {
    }

    bool isMandatory() const override { return true; }

    void execute() override
    {
        if (isCompleted())
            return;

        // 1) First tick: publish + subscribe
        if (!isStarted())
        {
            markStarted();
            Serial.println(F("[TRIP] ValidateTripWithServerTask: start (MQTT)"));

            if (!requestTopic || !responseTopic)
            {
                Serial.println(F("[TRIP] Missing MQTT topic(s)"));
                lcdLine1 = "Validation failed";
                lcdLine2 = "Bad config";
                markCompleted();
                return;
            }

            // Set ourself as the active validation task so the global callback can route messages
            g_activeValidationTask = this;

            // Subscribe to response topic
            if (!gsm.mqtt.subscribe(responseTopic))
            {
                Serial.println(F("[TRIP] MQTT subscribe failed"));
                lcdLine1 = "Validation failed";
                lcdLine2 = "Subscribe error";
                markCompleted();
                g_activeValidationTask = nullptr;
                return;
            }

            Serial.print(F("[TRIP] Subscribed to: "));
            Serial.println(responseTopic);

            // Encode Trip → binary
            uint8_t buffer[256];
            int len = encodeTrip(trip, buffer);
            if (len <= 0)
            {
                Serial.println(F("[TRIP] encodeTrip produced empty payload"));
                lcdLine1 = "Validation failed";
                lcdLine2 = "Encode error";
                markCompleted();
                g_activeValidationTask = nullptr;
                gsm.mqtt.unsubscribe(responseTopic);
                return;
            }

            // Publish request
            bool ok = gsm.publishMqtt(buffer, static_cast<size_t>(len), requestTopic);
            if (!ok)
            {
                Serial.println(F("[TRIP] MQTT publish FAILED"));
                lcdLine1 = "Validation failed";
                lcdLine2 = "MQTT error";
                markCompleted();
                g_activeValidationTask = nullptr;
                gsm.mqtt.unsubscribe(responseTopic);
                return;
            }

            Serial.print(F("[TRIP] MQTT publish OK, len="));
            Serial.println(len);

            lcdLine1 = "Sending validation";
            lcdLine2 = "Waiting response...";
            awaitingResponse = true;

            // First tick ends here; we'll wait for response in next ticks
            return;
        }

        // 2) After first tick: keep MQTT pumping & wait for response
        gsm.stepMqtt(); // pump incoming MQTT so callback is invoked

        if (responseReceived)
        {
            // We have a result from server
            finishFromResponse();
            return;
        }

        // 3) Timeout if server never answers
        uint32_t elapsed = millis() - getStartMs();
        if (elapsed > overallTimeoutMs)
        {
            Serial.println(F("[TRIP] Validation timeout (no response)"));
            lcdLine1 = "Validation timeout";
            lcdLine2 = "Please try again";

            // Cleanup
            gsm.mqtt.unsubscribe(responseTopic);
            if (g_activeValidationTask == this)
                g_activeValidationTask = nullptr;

            markCompleted();
        }
    }

    // Called from global MQTT callback when a message arrives
    void onMqttMessage(const char *topic, const uint8_t *payload, unsigned int length)
    {
        if (!awaitingResponse || isCompleted())
            return;

        if (strcmp(topic, responseTopic) != 0)
            return; // không phải topic của mình

        Serial.print(F("[TRIP] Validation response on topic: "));
        Serial.println(topic);

        TripValidationResponse resp;
        bool ok = decodeTripValidationResponse(payload, length, resp);
        if (!ok)
        {
            Serial.println(F("[TRIP] Failed to decode TripValidationResponse"));
            lcdLine1 = "Validation failed";
            lcdLine2 = "Please try again...";
            // vẫn coi như đã nhận response (nhưng fail)
            validationSuccess = false;
            responseReceived = true;
            return;
        }

        if (resp.isValid)
        {
            Serial.println(F("[TRIP] Reservation is VALID"));
            validationSuccess = true;
            lcdLine1 = "reservation validated!";
            lcdLine2 = "Enjoy your ride!";
        }
        else
        {
            Serial.println(F("[TRIP] Reservation is NOT valid"));
            validationSuccess = false;
            lcdLine1 = "Bike validation failed";
            lcdLine2 = "Please try again";
        }

        // ==> CỰC KỲ QUAN TRỌNG
        responseReceived = true;
    }

private:
    void finishFromResponse()
    {
        Serial.println(F("[TRIP] finishFromResponse()"));

        gsm.mqtt.unsubscribe(responseTopic);
        if (g_activeValidationTask == this)
            g_activeValidationTask = nullptr;

        if (!validationSuccess)
        {
            Serial.println(F("[TRIP] Validation FAILED"));
            lcdLine1 = "Bike validation";
            lcdLine2 = "failed";
            markCompleted();
            return;
        }

        // Success: cập nhật trip id & state
        tripIdRef = trip.id; // hoặc server gửi id nào đó thì dùng id đó
        bikeStateRef = INUSED;

        lcdLine1 = "Trip Validated!";
        lcdLine2 = "Enjoy your ride!";

        markCompleted();
    }
};