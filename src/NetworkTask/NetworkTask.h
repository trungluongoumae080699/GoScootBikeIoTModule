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

protected:
    // ---------------------------------------------------------
    // State for non-blocking tasks
    // ---------------------------------------------------------
    bool     started   = false;
    bool     completed = false;
    uint32_t startMs   = 0;   // used for timeout inside execute()

public:
    // ---------------------------------------------------------
    // Lifecycle helpers (now virtual so children can override)
    // ---------------------------------------------------------
    virtual void markStarted()
    {
        if (!started)
        {
            started = true;
            startMs = millis();
        }
    }

    virtual void markCompleted()
    {
        completed = true;
    }

    bool isStarted() const    { return started; }
    bool isCompleted() const  { return completed; }
    uint32_t getStartMs() const { return startMs; }
};