#include <Arduino.h>
#include <ESP32QRCodeReader.h>
void setup() {
  Serial.begin(9600); // GPIO1 TX, GPIO3 RX
}

void loop() {
  Serial.println(1);

}