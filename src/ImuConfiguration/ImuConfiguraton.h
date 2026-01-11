#pragma once
#include <Wire.h>
#include <MPU6050.h>
#include <math.h>

// -----------------------------------------------------------
// Vehicle states (simple + practical)
// -----------------------------------------------------------
enum class VehicleState : uint8_t {
    UNKNOWN = 0,
    UPRIGHT,        // z1 in [0.8 .. 1.0]
    TILTED,         // z1 in [0.7 .. 0.8)
    ON_SIDE,        // z1 in [0.0 .. 0.7)
    UPSIDE_DOWN     // z1 < 0.0
};

struct ImuConfiguration {
    MPU6050 imu;
    bool initialized = false;

    // --- External variable references (raw sensor) ---
    int16_t &accelX;
    int16_t &accelY;
    int16_t &accelZ;

    int16_t &gyroRollRate;
    int16_t &gyroPitchRate;
    int16_t &gyroYawRate;

    // --- External references (requested) ---
    VehicleState &currentState;          // confirmed/published state
    unsigned long &znStableSinceMs;      // timestamp when current rounded z1 started being stable
    float &znRounded1dp;                 // latest z_n rounded to 1 decimal (z1)

    // --- Internal tracking ---
    float _lastZ1 = 999.0f;              // impossible init value
    VehicleState _candidateState = VehicleState::UNKNOWN;

    // --- Time requirements (tune as needed) ---
    static constexpr unsigned long UPRIGHT_HOLD_MS     = 2000UL;
    static constexpr unsigned long TILTED_HOLD_MS      = 1000UL;
    static constexpr unsigned long ON_SIDE_HOLD_MS     = 3000UL;
    static constexpr unsigned long UPSIDE_DOWN_HOLD_MS = 30000UL;

    // -----------------------------------------------------------
    // Constructor binds references to caller variables
    // -----------------------------------------------------------
    ImuConfiguration(
        int16_t &accelXRef, int16_t &accelYRef, int16_t &accelZRef,
        int16_t &gyroRollRateRef, int16_t &gyroPitchRateRef, int16_t &gyroYawRateRef,
        VehicleState &currentStateRef,
        unsigned long &znStableSinceMsRef,
        float &znRounded1dpRef
    )
        : accelX(accelXRef), accelY(accelYRef), accelZ(accelZRef),
          gyroRollRate(gyroRollRateRef), gyroPitchRate(gyroPitchRateRef), gyroYawRate(gyroYawRateRef),
          currentState(currentStateRef),
          znStableSinceMs(znStableSinceMsRef),
          znRounded1dp(znRounded1dpRef)
    {}

    // -----------------------------------------------------------
    void begin() {
        imu.initialize();
        initialized = imu.testConnection();
        Serial.println(initialized ? "IMU ready" : "IMU connection failed");

        if (initialized) {
            currentState = VehicleState::UPRIGHT;
            _candidateState = VehicleState::UPRIGHT;

            znRounded1dp = 0.0f;
            _lastZ1 = 1.0f;               // forces first reset
            znStableSinceMs = millis();
        }
    }

    // -----------------------------------------------------------
    bool update() {
        if (!initialized) return false;

        imu.getMotion6(&accelX, &accelY, &accelZ,
                       &gyroRollRate, &gyroPitchRate, &gyroYawRate);

        updateStateFromAccel();
        return true;
    }

    // -----------------------------------------------------------
    static float round1dp(float v) {
        return roundf(v * 10.0f) / 10.0f;
    }

    // Classification rule you specified:
    //  - 0.8..1.0 : UPRIGHT
    //  - 0.7..0.8 : TILTED
    //  - 0.0..0.7 : ON_SIDE
    //  - < 0.0    : UPSIDE_DOWN
    static VehicleState classifyByZ1(float z1) {
        if (z1 >= 0.8f) return VehicleState::UPRIGHT;
        if (z1 >= 0.7f) return VehicleState::TILTED;
        if (z1 >= 0.0f) return VehicleState::ON_SIDE;
        return VehicleState::UPSIDE_DOWN;
    }

    static unsigned long requiredHoldMs(VehicleState s) {
        switch (s) {
            case VehicleState::UPRIGHT:      return UPRIGHT_HOLD_MS;
            case VehicleState::TILTED:       return TILTED_HOLD_MS;
            case VehicleState::ON_SIDE:      return ON_SIDE_HOLD_MS;
            case VehicleState::UPSIDE_DOWN:  return UPSIDE_DOWN_HOLD_MS;
            default:                         return 0UL;
        }
    }

    // -----------------------------------------------------------
    void updateStateFromAccel() {
        // Compute magnitude A
        float A = sqrtf(
            (float)accelX * accelX +
            (float)accelY * accelY +
            (float)accelZ * accelZ
        );
        if (A <= 0.0001f) return;

        // Compute normalized z and round to 1 decimal
        float z_n = accelZ / A;
        float z1 = round1dp(z_n);
        znRounded1dp = z1;

        unsigned long now = millis();

        // Reset timer whenever rounded z1 changes
        if (z1 != _lastZ1) {
            _lastZ1 = z1;
            znStableSinceMs = now;
        }

        // Candidate state is based on current z1
        VehicleState candidate = classifyByZ1(z1);

        // If candidate state changed, restart timing (must be stable in the new state)
        if (candidate != _candidateState) {
            _candidateState = candidate;
            znStableSinceMs = now; // reset stability timer for this new candidate
        }

        unsigned long stableForMs = now - znStableSinceMs;
        unsigned long needMs = requiredHoldMs(_candidateState);

        // Confirm only if stable long enough
        if (needMs > 0UL && stableForMs >= needMs) {
            currentState = _candidateState;
        }
    }

    // -----------------------------------------------------------
    const char* stateToString(VehicleState s) const {
        switch (s) {
            case VehicleState::UPRIGHT:      return "UPRIGHT";
            case VehicleState::TILTED:       return "TILTED";
            case VehicleState::ON_SIDE:      return "ON_SIDE";
            case VehicleState::UPSIDE_DOWN:  return "UPSIDE_DOWN";
            default:                         return "UNKNOWN";
        }
    }

    // -----------------------------------------------------------
    void printDebug() {
        if (!initialized) return;

        // Compute magnitude A
        float A = sqrtf(
            (float)accelX * accelX +
            (float)accelY * accelY +
            (float)accelZ * accelZ
        );

        float x_n = 0, y_n = 0, z_n = 0;
        if (A > 0.0001f) {
            x_n = accelX / A;
            y_n = accelY / A;
            z_n = accelZ / A;
        }

        float z1 = round1dp(z_n); // shown value (1 decimal)

        Serial.print("Accel (X,Y,Z): ");
        Serial.print(accelX); Serial.print(", ");
        Serial.print(accelY); Serial.print(", ");
        Serial.print(accelZ);

        Serial.print(" | Accel_n (X,Y,Z): ");
        Serial.print(x_n, 1); Serial.print(", ");
        Serial.print(y_n, 1); Serial.print(", ");
        Serial.print(z1, 1);

        Serial.print(" | Gyro (Roll,Pitch,Yaw): ");
        Serial.print(gyroRollRate); Serial.print(", ");
        Serial.print(gyroPitchRate); Serial.print(", ");
        Serial.print(gyroYawRate);

        Serial.print(" | State: ");
        Serial.print(stateToString(currentState));

        Serial.print(" | Stable(ms): ");
        Serial.print(millis() - znStableSinceMs);

        Serial.println();
    }
};