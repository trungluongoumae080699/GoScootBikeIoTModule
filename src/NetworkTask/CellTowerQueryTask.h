#pragma once

#include <Arduino.h>
#include "NetworkTask.h"
#include "NetworkConfiguration/GsmConfiguration.h"
#include "Domains/CellInfo.h"

class CellTowerQueryTask : public NetworkTask
{
public:
    // Task will fill outCell directly
    explicit CellTowerQueryTask(GsmConfiguration &gsmRef,
                                CellInfo &outputCellInfo,
                                uint32_t timeoutMs = 5000)
        : gsm(gsmRef),
          outCell(outputCellInfo),
          timeoutMs(timeoutMs)
    {
    }

    // This is still a nice-to-have, can be dropped if queue is full
    bool isMandatory() const override { return false; }

    void execute() override
    {
        if (isCompleted())
            return; // already done, nothing to do

        // ----------------- FIRST CALL: send +CPSI? -----------------
        if (!isStarted())
        {
            markStarted();

            successFlag = false;
            jsonResult  = "";
            cpsiLine    = "";

            // Clean modem RX buffer
            while (gsm.modem.stream.available())
            {
                gsm.modem.stream.read();
            }

            gsm.modem.sendAT("+CPSI?");
            Serial.println(F("[CELL] +CPSI? sent (non-blocking task)"));

            return; // yield, wait for response in later execute() calls
        }

        // ----------------- SUBSEQUENT CALLS: read response -----------------
        // Drain whatever bytes are available this loop
        while (gsm.modem.stream.available())
        {
            String line = gsm.modem.stream.readStringUntil('\n');
            line.trim();
            if (line.length() == 0)
                continue;

            Serial.print(F("[CELL] LINE: "));
            Serial.println(line);

            if (line.startsWith("+CPSI"))
            {
                cpsiLine = line;
            }

            if (line == "OK")
            {
                // End of response → finalize
                finalizeFromCpsi();
                markCompleted();
                return;
            }

            if (line.indexOf("ERROR") >= 0)
            {
                Serial.println(F("[CELL] CPSI ERROR"));
                successFlag = false;
                markCompleted();
                return;
            }
        }

        // ----------------- TIMEOUT CHECK -----------------
        if (millis() - getStartMs() > timeoutMs)
        {
            Serial.println(F("[CELL] CPSI timeout"));
            finalizeFromCpsi(); // try to parse if we did get a +CPSI line
            markCompleted();
        }
    }

    // exposed results
    bool success() const { return successFlag; }
    const String &getJson() const { return jsonResult; }

    // optional: reuse base isStarted/isCompleted but keep these aliases if you like
    bool isStartedLocal()   const { return isStarted(); }
    bool isCompletedLocal() const { return isCompleted(); }

protected:
    // override to add logging but still call base to set the flag
    void markStarted() override
    {
        NetworkTask::markStarted();
        // (optional extra logs)
        // Serial.println(F("[CELL] CellTowerQueryTask started"));
    }

    void markCompleted() override
    {
        Serial.println(F("[CELL] CellTowerQueryTask completed"));
        NetworkTask::markCompleted();
    }

private:
    GsmConfiguration &gsm;
    CellInfo &outCell; // external object we update

    // internal state for this async task
    bool   successFlag = false;
    String jsonResult;
    String cpsiLine;

    uint32_t timeoutMs = 2000; // default 2s, ctor may override

    void finalizeFromCpsi()
    {
        if (cpsiLine.length() == 0)
        {
            Serial.println(F("[CELL] No +CPSI line to parse"));
            successFlag = false;
            return;
        }

        Serial.print(F("[CELL] Parsing CPSI: "));
        Serial.println(cpsiLine);

        // Fill the provided CellInfo reference
        outCell.parseCpsiLine(cpsiLine);

        jsonResult = outCell.buildLocationApiJson();
        if (jsonResult.length() == 0)
        {
            Serial.println(F("[CELL] buildLocationApiJson returned empty JSON"));
            successFlag = false;
            return;
        }

        Serial.println(F("[CELL] Cell JSON:"));
        Serial.println(jsonResult);

        successFlag = true;
    }
};