#include <Adafruit_INA219.h>

Adafruit_INA219 ina219;

void setup() {
  Serial.begin(115200);
  ina219.begin();
}

void loop() {
  float busVoltage = ina219.getBusVoltage_V();      // V
  float shuntVoltage = ina219.getShuntVoltage_mV(); // mV
  float current_mA = ina219.getCurrent_mA();        // mA
  float power_mW = ina219.getPower_mW();            // mW

  Serial.print("Bus Voltage: "); Serial.print(busVoltage); Serial.println(" V");
  Serial.print("Shunt Voltage: "); Serial.print(shuntVoltage); Serial.println(" mV");
  Serial.print("Current: "); Serial.print(current_mA); Serial.println(" mA");
  Serial.print("Power: "); Serial.print(power_mW); Serial.println(" mW");
  Serial.println();

  delay(1000);
}