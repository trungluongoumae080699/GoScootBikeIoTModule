#include <Arduino.h>
#include <Wire.h>
#include "ImuConfiguration/ImuConfiguraton.h"

// -------------------------------
// Raw IMU values
// -------------------------------
int16_t accelX = 0;
int16_t accelY = 0;
int16_t accelZ = 0;

int16_t gyroRollRate = 0;
int16_t gyroPitchRate = 0;
int16_t gyroYawRate = 0;

// -------------------------------
// State tracking (NEW)
// -------------------------------
VehicleState currentState = VehicleState::UNKNOWN;
unsigned long znStableSinceMs = 0;
float znRounded1dp = 0.0f;

// -------------------------------
// IMU configuration instance
// -------------------------------
ImuConfiguration imu(
    accelX, accelY, accelZ,
    gyroRollRate, gyroPitchRate, gyroYawRate,
    currentState,
    znStableSinceMs,
    znRounded1dp
);

// -------------------------------
void setup()
{
    Serial.begin(115200);
    while (!Serial) { }     // useful on some boards (harmless on Mega)

    Wire.begin();           // I2C init (Mega uses pins 20/21)
    imu.begin();

    Serial.println("IMU test started");
}

// -------------------------------
void loop()
{
    if (imu.update())
    {
        imu.printDebug();
    }
    else
    {
        Serial.println("IMU not initialized");
    }
    Serial.println(currentState == VehicleState::UPRIGHT ? "Vehicle is UPRIGHT" : "Vehicle is NOT UPRIGHT");
    delay(1000); // 1 Hz update for easy reading
}