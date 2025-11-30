#include <Arduino.h>
#include <Trip.h>
#include <WiFi.h>

void setup() {
  Serial.begin(115200); // to Arduino
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); // no need to connect
}

void loop() {
/*   if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "SCAN") {
      int n = WiFi.scanNetworks(false, true);
      
      Serial.printf("WIFI:%d\n", n);
      for (int i = 0; i < n; i++) {
        Serial.printf("%s,%d\n",
          WiFi.BSSIDstr(i).c_str(),
          WiFi.RSSI(i)
        );
      }
      Serial.println("END");
    }
  } */

  int n = WiFi.scanNetworks(false, true);
      
      Serial.printf("WIFI:%d\n", n);
      for (int i = 0; i < n; i++) {
        Serial.printf("%s,%d\n",
          WiFi.BSSIDstr(i).c_str(),
          WiFi.RSSI(i)
        );
      }
      Serial.println("END");
      delay(1000);
}