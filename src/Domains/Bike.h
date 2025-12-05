#ifndef BIKE_H
#define BIKE_H

#include <Arduino.h>
#include <stdint.h>

struct Bike {
  String userName;
  String password;
  float longitude;
  float latitude;
  int32_t battery;
};

enum BikeState {
    IDLE,
    INUSED,
    OUT_OF_BOUND
};



#endif