#pragma once

#include <Arduino.h>
#include <stdint.h>

struct Bike {
  String userName;
  String password;
  String current_hub;
};

enum UsageState {
    IDLE = 0,
    RESERVED = 1,
    INUSED = 2
};

enum OperationState {
    NORMAL = 0,
    OUT_OF_BOUND = 1,
    LOW_BATTERY = 2
};




