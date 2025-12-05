// TEST: raw AT +CPSI? on Serial2
#include <Arduino.h>

void setup() {
  Serial.begin(115200);      // USB serial to your PC
  Serial2.begin(115200);     // SIM7600 UART

  delay(3000);
  Serial.println("=== AT test ===");

  Serial2.println("AT");
  delay(500);
  while (Serial2.available()) {
    char c = Serial2.read();
    Serial.write(c);
  }

  Serial.println("\n=== AT+CPSI? ===");
  Serial2.println("AT+CPSI?");
  unsigned long start = millis();
  while (millis() - start < 5000) {
    while (Serial2.available()) {
      char c = Serial2.read();
      Serial.write(c);      // echo modem reply to monitor
    }
  }
}

void loop() {}