#pragma once
#include <Wire.h>
#include <MPU6050.h>
struct ImuConfiguration {
    
    MPU6050 imu;
    bool initialized = false;

    // --- External variable references (readable names) ---
    int16_t &accelX;
    int16_t &accelY;
    int16_t &accelZ;

    int16_t &gyroRollRate;
    int16_t &gyroPitchRate;
    int16_t &gyroYawRate;

    // -----------------------------------------------------------
    // Constructor binds references to caller variables
    // -----------------------------------------------------------
    ImuConfiguration(int16_t &accelXRef, int16_t &accelYRef, int16_t &accelZRef,
                     int16_t &gyroRollRateRef, int16_t &gyroPitchRateRef, int16_t &gyroYawRateRef)
        : accelX(accelXRef), accelY(accelYRef), accelZ(accelZRef),
          gyroRollRate(gyroRollRateRef), gyroPitchRate(gyroPitchRateRef), gyroYawRate(gyroYawRateRef)
    {}

    // -----------------------------------------------------------
    void begin() {
        Wire.begin();
        imu.initialize();
        initialized = imu.testConnection();
        Serial.println(initialized ? "IMU ready" : "IMU connection failed");
    }

    // -----------------------------------------------------------
    bool update() {
        if (!initialized) return false;

        imu.getMotion6(&accelX, &accelY, &accelZ,
                       &gyroRollRate, &gyroPitchRate, &gyroYawRate);

        return true;
    }

    // -----------------------------------------------------------
    void printDebug() {
        if (!initialized) return;

        Serial.print("Accel (X,Y,Z): ");
        Serial.print(accelX); Serial.print(", ");
        Serial.print(accelY); Serial.print(", ");
        Serial.print(accelZ);

        Serial.print(" | Gyro (Roll,Pitch,Yaw): ");
        Serial.print(gyroRollRate); Serial.print(", ");
        Serial.print(gyroPitchRate); Serial.print(", ");
        Serial.print(gyroYawRate);

        Serial.println();
    }
};