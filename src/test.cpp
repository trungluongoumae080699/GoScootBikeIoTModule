#define CUSTOM_SETTINGS
#define INCLUDE_GAMEPAD_MODULE

#include <SPI.h>
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Dabble.h>

#define STRAIGHT_IN1 4
#define STRAIGHT_IN2 7
#define BACK_IN1 5
#define BACK_IN2 6

void setup()
{
  Serial.begin(115200);

  pinMode(STRAIGHT_IN1, OUTPUT);
  pinMode(STRAIGHT_IN2, OUTPUT);
  pinMode(BACK_IN1, OUTPUT);
  pinMode(BACK_IN2, OUTPUT);
  digitalWrite(STRAIGHT_IN1, LOW);
  digitalWrite(STRAIGHT_IN2, LOW);
  digitalWrite(BACK_IN1, LOW);
  digitalWrite(BACK_IN2, LOW);
  Serial.begin(115200);
  Dabble.begin(9600, Serial3);
  Serial.println("Dabble Gamepad Ready.");
  Serial.println("HM-10 test running...");

  Serial.println("L298N Motor Test Starting...");
}

void loop()
{
  digitalWrite(STRAIGHT_IN1, LOW);
  digitalWrite(STRAIGHT_IN2, LOW);
  digitalWrite(BACK_IN1, LOW);
  digitalWrite(BACK_IN2, LOW);
  Dabble.processInput();
  // Read from HM-10 → Serial Monitor
  if (GamePad.isUpPressed()){
    digitalWrite(STRAIGHT_IN1, HIGH);
    digitalWrite(STRAIGHT_IN2, HIGH);
    digitalWrite(BACK_IN1, LOW);
    digitalWrite(BACK_IN2, LOW);
  }
  if (GamePad.isDownPressed()){
    digitalWrite(STRAIGHT_IN1, LOW);
    digitalWrite(STRAIGHT_IN2, LOW);
    digitalWrite(BACK_IN1, HIGH);
    digitalWrite(BACK_IN2, HIGH);
  }
  if (GamePad.isLeftPressed())
    Serial.println("LEFT");
  if (GamePad.isRightPressed())
    Serial.println("RIGHT");

  if (GamePad.isSquarePressed())
    Serial.println("SQUARE");
  if (GamePad.isCirclePressed())
    Serial.println("CIRCLE");
  if (GamePad.isTrianglePressed())
    Serial.println("TRIANGLE");
  if (GamePad.isCrossPressed())
    Serial.println("CROSS");
}
