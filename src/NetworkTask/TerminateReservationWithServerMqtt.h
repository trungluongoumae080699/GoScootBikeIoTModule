#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "NetworkTask.h"
#include "NetworkConfiguration/GsmConfiguration.h"
#include "Domains/Bike.h"
#include "Domains/Trip.h"
#include "UI/DisplayTask.h"

// Forward declaration so global callback can see it
class TerminateReservationWithServerMqtt;
extern TerminateReservationWithServerMqtt *g_activeTripTerminationTask;

class TerminateReservationWithServerMqtt : public NetworkTask
{
private:
    GsmConfiguration &gsm;
    TripTerminationPayload tripTerminationPayload;                 // trip info we send to server
    const char *requestTopic;  // e.g. "/reservation/validation/BIK_298A1J35"
    const char *responseTopic; // e.g. "/reservation/BIK_298A1J35/validation/response"
    String &tripIdRef;
    UsageState &bikeStateRef;
    DisplayPage &currentDisplayedPage;
    DisplayPage &prevDisplayedPage;
    bool &toUpdateDisplay;

    bool awaitingResponse = false;
    bool responseReceived = false;
    bool terminationSuccess = false;
    ;
    String serverIdOrMsg;

    uint32_t overallTimeoutMs = 15000; // 15s for full request+response

public:
    TerminateReservationWithServerMqtt(GsmConfiguration &gsmRef,
                                   const TripTerminationPayload &tripIn,
                                   const char *requestTopicIn,
                                   const char *responseTopicIn,
                                   String &tripIdOut,
                                   UsageState &bikeStateOut,
                                   DisplayPage &currentDisplayedPage,
                                   DisplayPage &prevDisplayedPage,
                                   bool &toUpdateDisplayOut

                                   )
        : gsm(gsmRef),
          tripTerminationPayload(tripIn),
          requestTopic(requestTopicIn),
          responseTopic(responseTopicIn),
          tripIdRef(tripIdOut),
          bikeStateRef(bikeStateOut),
          currentDisplayedPage(currentDisplayedPage),
          prevDisplayedPage(prevDisplayedPage),
          toUpdateDisplay(toUpdateDisplayOut)

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
            Serial.println(F("[TRIP] Terminating Trip With server start (MQTT)"));

            if (!requestTopic || !responseTopic)
            {
                Serial.println(F("[TRIP] Missing MQTT topic(s)"));
                currentDisplayedPage = DisplayPage::GenericAlert;
                markCompleted();
                return;
            }

            // Set ourself as the active validation task so the global callback can route messages
            g_activeTripTerminationTask = this;

            // Subscribe to response topic
            if (!gsm.mqtt.subscribe(responseTopic))
            {
                Serial.println(F("[TRIP] MQTT subscribe failed"));
                currentDisplayedPage = DisplayPage::GenericAlert;
                markCompleted();
                g_activeTripTerminationTask = nullptr;
                return;
            }

            Serial.print(F("[TRIP] Subscribed to: "));
            Serial.println(responseTopic);

            // Encode Trip → binary
            uint8_t buffer[256];
            int len = encodeTripTerminationPayload(tripTerminationPayload, buffer);
            if (len <= 0)
            {
                Serial.println(F("[TRIP] encodeTrip produced empty payload"));
                currentDisplayedPage = DisplayPage::GenericAlert;
                markCompleted();
                g_activeTripTerminationTask = nullptr;
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
                g_activeTripTerminationTask = nullptr;
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
            Serial.println(F("[TRIP] Termination timeout (no response)"));
            currentDisplayedPage = DisplayPage::GenericAlert;

            // Cleanup
            gsm.mqtt.unsubscribe(responseTopic);
            if (g_activeTripTerminationTask == this)
                g_activeTripTerminationTask = nullptr;

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

        Serial.print(F("[TRIP] Termination response on topic: "));
        Serial.println(topic);
        int status = decodeTripStatusUpdate(payload, length);
        if (status < 0)
        {
            Serial.println(F("[TRIP] Failed to decode payload"));
            currentDisplayedPage = DisplayPage::GenericAlert;
            // vẫn coi như đã nhận response (nhưng fail)
            terminationSuccess = false;
            responseReceived = true;
            return;
        }

        if (status == 2)
        {
            Serial.println(F("[TRIP] Termination is SUCCESSFUL"));
            terminationSuccess = true;
        }
        else
        {
            Serial.println(F("[TRIP] Termination FAILED"));
            terminationSuccess = false;
        }

        // ==> CỰC KỲ QUAN TRỌNG
        responseReceived = true;
    }

private:
    void finishFromResponse()
    {
        Serial.println(F("[TRIP] finishFromResponse()"));

        gsm.mqtt.unsubscribe(responseTopic);
        if (g_activeTripTerminationTask == this)
            g_activeTripTerminationTask = nullptr;
        tripIdRef = "";

        if (!terminationSuccess)
        {
            Serial.println(F("[TRIP] TERMINATION FAILED"));
            currentDisplayedPage = DisplayPage::TripConclusionFailed;
            toUpdateDisplay = true;
            tripIdRef = "";
            markCompleted();
            return;
        }
        else
        {
            Serial.println(F("[TRIP] TERMINATION SUCCEEDED"));
            toUpdateDisplay = true;
            currentDisplayedPage = DisplayPage::TripConclusion;
            markCompleted();
        }

        // Success: cập nhật trip id & state
    }
};