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
    UsageState &bikeStateRef;
    DisplayPage &currentDisplayedPage;


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
                                   UsageState &bikeStateOut,
                                   DisplayPage &currentDisplayedPage
                            
                                   )
        : gsm(gsmRef),
          trip(tripIn),
          requestTopic(requestTopicIn),
          responseTopic(responseTopicIn),
          tripIdRef(tripIdOut),
          bikeStateRef(bikeStateOut),
          currentDisplayedPage(currentDisplayedPage)
          
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
                currentDisplayedPage = DisplayPage::GenericAlert;
                markCompleted();
                return;
            }

            // Set ourself as the active validation task so the global callback can route messages
            g_activeValidationTask = this;

            // Subscribe to response topic
            if (!gsm.mqtt.subscribe(responseTopic))
            {
                Serial.println(F("[TRIP] MQTT subscribe failed"));
                currentDisplayedPage = DisplayPage::GenericAlert;
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
                currentDisplayedPage = DisplayPage::GenericAlert;
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
                currentDisplayedPage = DisplayPage::GenericAlert;
                markCompleted();
                g_activeValidationTask = nullptr;
                gsm.mqtt.unsubscribe(responseTopic);
                return;
            }

            Serial.print(F("[TRIP] MQTT publish OK, len="));
            Serial.println(len);

            
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
            currentDisplayedPage = DisplayPage::GenericAlert;

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
            currentDisplayedPage = DisplayPage::GenericAlert;
            // vẫn coi như đã nhận response (nhưng fail)
            validationSuccess = false;
            responseReceived = true;
            return;
        }

        if (resp.isValid)
        {
            Serial.println(F("[TRIP] Reservation is VALID"));
            validationSuccess = true;

        }
        else
        {
            Serial.println(F("[TRIP] Reservation is NOT valid"));
            validationSuccess = false;


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
            currentDisplayedPage = DisplayPage::GenericAlert;
            markCompleted();
            return;
        }

        // Success: cập nhật trip id & state
        tripIdRef = trip.id; // hoặc server gửi id nào đó thì dùng id đó
        bikeStateRef = UsageState::INUSED;

       currentDisplayedPage = DisplayPage::Welcome;

        markCompleted();
    }
};