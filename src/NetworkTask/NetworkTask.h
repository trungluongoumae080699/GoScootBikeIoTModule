#pragma once
#include <Arduino.h>

struct NetworkTask
{
    virtual ~NetworkTask() {}

    // ---------------------------------------------------------
    // Mandatory function: called repeatedly until completed
    // ---------------------------------------------------------
    virtual void execute() = 0;

    // ---------------------------------------------------------
    // Task priority policy (optional)
    // ---------------------------------------------------------
    virtual bool isMandatory() const { return false; }

    // ---------------------------------------------------------
    // State control for non-blocking tasks
    // ---------------------------------------------------------
protected:
    bool started   = false;
    bool completed = false;
    uint32_t startMs = 0;   // used for timeout inside execute()

public:
    // Called by task internally the first time it runs
    void markStarted()
    {
        started = true;
        startMs = millis();
    }

    // Called by task internally when done
    void markCompleted()
    {
        completed = true;
    }

    bool isStarted() const { return started; }
    bool isCompleted() const { return completed; }

    uint32_t getStartMs() const { return startMs; }
};