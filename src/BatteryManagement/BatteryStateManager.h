#include <Arduino.h>
#include <EEPROM.h>
#include <Adafruit_INA219.h>

struct BatteryStateManager {
    Adafruit_INA219 &ina;
    int &batteryLevel;        // Output % (0–100)

    // ===== Battery configuration =====
    const float MAX_VOLTAGE = 8.40f;   // Fully charged 2S
    const float MAX_MAH     = 3200.0f; // Your battery capacity
    const float VOLT_DIFF_THRESHOLD = 0.20f; // >200mV difference → external charge/swap

    // ===== EEPROM layout =====
    const uint16_t EEPROM_MAGIC = 0xBEEF;
    const int ADDR_MAGIC        = 0;
    const int ADDR_HIGHEST_V    = ADDR_MAGIC + sizeof(uint16_t);
    const int ADDR_MAHUSED      = ADDR_HIGHEST_V + sizeof(float);

    // ===== Runtime state variables =====
    float highestRecordedVoltage = 0.0f;
    float mAhUsed = 0.0f;               // Coulomb counter
    uint32_t lastUpdateMs = 0;
    uint32_t lastEepromSaveMs = 0;
    const uint32_t SAVE_INTERVAL_MS = 120000UL; // 2 minutes

    // Constructor
    BatteryStateManager(Adafruit_INA219 &inaRef, int &levelRef)
        : ina(inaRef), batteryLevel(levelRef) {}

    // ============================================================
    //  STARTUP LOGIC — called inside setup()
    // ============================================================
    void begin() {
        float V_now = ina.getBusVoltage_V();

        bool hasValidEEPROM = loadFromEEPROM();

        if (hasValidEEPROM) {
            float diff = fabs(V_now - highestRecordedVoltage);

            if (diff < VOLT_DIFF_THRESHOLD) {
                // Voltage close → assume same battery state
                batteryLevel = computeSOCfromMah(mAhUsed);
            } else {
                // Voltage changed a lot → swapped or externally charged
                resetFromVoltage(V_now);
                saveToEEPROM();
            }
        } else {
            // No valid data → first boot or corrupted EEPROM
            resetFromVoltage(V_now);
            saveToEEPROM();
        }

        lastUpdateMs = millis();
        lastEepromSaveMs = millis();
    }

    // ============================================================
    //  RUNTIME LOGIC — called every loop()
    // ============================================================
    void update() {
        uint32_t now = millis();
        float currentVoltage = ina.getBusVoltage_V();
        float current_mA = ina.getCurrent_mA(); // + discharge, - charge

        // Time delta (hours)
        float deltaHours = 0;
        if (lastUpdateMs > 0) {
            deltaHours = (now - lastUpdateMs) / 3600000.0f;
        }
        lastUpdateMs = now;

        // ---- COULOMB COUNTING ----
        mAhUsed += current_mA * deltaHours;   // charging = negative current

        // Clamp
        if (mAhUsed < 0) mAhUsed = 0;
        if (mAhUsed > MAX_MAH) mAhUsed = MAX_MAH;

        // ---- UPDATE BATTERY LEVEL ----
        batteryLevel = computeSOCfromMah(mAhUsed);

        // ---- TRACK HIGHEST VOLTAGE ----
        if (currentVoltage > highestRecordedVoltage) {
            highestRecordedVoltage = currentVoltage;
        }

        // ---- PERIODIC EEPROM SAVE ----
        if (now - lastEepromSaveMs >= SAVE_INTERVAL_MS) {
            saveToEEPROM();
            lastEepromSaveMs = now;
        }
    }

private:

    // ============================================================
    //  VOLTAGE → SOC estimation (startup only)
    //  Using lookup + linear interpolation for 2S Li-ion
    // ============================================================
    float estimateSOC_FromVoltage(float vPack) {
        const int N = 11;
        const float voltTable[N] = {
            6.40f, 6.70f, 6.90f, 7.10f, 7.30f,
            7.50f, 7.70f, 7.90f, 8.10f, 8.30f, 8.40f
        };
        const float socTable[N] = {
             0.0f, 10.0f, 20.0f, 30.0f, 40.0f,
            50.0f, 60.0f, 70.0f, 80.0f, 95.0f, 100.0f
        };

        if (vPack <= voltTable[0]) return 0.0f;
        if (vPack >= voltTable[N-1]) return 100.0f;

        for (int i = 0; i < N - 1; i++) {
            if (vPack >= voltTable[i] && vPack <= voltTable[i+1]) {
                float t = (vPack - voltTable[i]) / (voltTable[i+1] - voltTable[i]);
                return socTable[i] + t * (socTable[i+1] - socTable[i]);
            }
        }
        return 0;
    }

    // ============================================================
    //  Reset coulomb counter based on voltage (startup)
    // ============================================================
    void resetFromVoltage(float v) {
        float soc = estimateSOC_FromVoltage(v);
        float remaining_mAh = (soc / 100.0f) * MAX_MAH;
        mAhUsed = MAX_MAH - remaining_mAh;

        batteryLevel = soc;
        highestRecordedVoltage = v;
    }

    // ============================================================
    //  Compute SOC % from mAhUsed
    // ============================================================
    int computeSOCfromMah(float used) {
        float remaining = MAX_MAH - used;
        float soc = (remaining / MAX_MAH) * 100.0f;

        if (soc < 0) soc = 0;
        if (soc > 100) soc = 100;
        return (int)round(soc);
    }

    // ============================================================
    //  EEPROM HANDLING
    // ============================================================
    bool loadFromEEPROM() {
        uint16_t magic;
        EEPROM.get(ADDR_MAGIC, magic);

        if (magic != EEPROM_MAGIC) {
            return false;
        }

        EEPROM.get(ADDR_HIGHEST_V, highestRecordedVoltage);
        EEPROM.get(ADDR_MAHUSED, mAhUsed);

        if (highestRecordedVoltage < 0 || highestRecordedVoltage > 20) return false;
        if (mAhUsed < 0 || mAhUsed > MAX_MAH) return false;

        return true;
    }

    void saveToEEPROM() {
        uint16_t magic = EEPROM_MAGIC;
        EEPROM.put(ADDR_MAGIC, magic);
        EEPROM.put(ADDR_HIGHEST_V, highestRecordedVoltage);
        EEPROM.put(ADDR_MAHUSED, mAhUsed);
    }
};