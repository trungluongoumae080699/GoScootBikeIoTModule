#define CUSTOM_SETTINGS
#define INCLUDE_GAMEPAD_MODULE

#include <SPI.h>
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Dabble.h>

#define STRAIGHT_LEFT A7
#define BACK_LEFT A6
#define STRAIGHT_RIGHT A5
#define BACK_RIGHT A4

//#define STRAIGHT_IN1 A3 //finalized
//#define BACK_IN2 A5 
//#define BACK_IN2 A46


void setup()
{
  Serial.begin(115200);

  pinMode(STRAIGHT_LEFT, OUTPUT);
  pinMode(BACK_LEFT, OUTPUT);
  pinMode(STRAIGHT_RIGHT, OUTPUT);
  pinMode(BACK_RIGHT, OUTPUT);

  digitalWrite(STRAIGHT_LEFT, LOW);
  digitalWrite(BACK_LEFT, LOW);
  digitalWrite(STRAIGHT_RIGHT, LOW);
  digitalWrite(BACK_RIGHT, LOW);

  //pinMode(STRAIGHT_IN2, OUTPUT);

  //pinMode(BACK_IN2, OUTPUT);
  
  //digitalWrite(STRAIGHT_IN2, LOW);
  
  //digitalWrite(BACK_IN2, LOW);

  Serial.begin(115200);
  Dabble.begin(9600, Serial3);

  Serial.println("Dabble Gamepad Ready.");
  Serial.println("HM-10 test running...");
  Serial.println("L298N Motor Test Starting...");
}

void loop()
{


  digitalWrite(STRAIGHT_LEFT, LOW);
  digitalWrite(STRAIGHT_RIGHT, LOW);
  digitalWrite(BACK_LEFT, LOW);
  digitalWrite(BACK_RIGHT, LOW);
  Dabble.processInput();
  // Read from HM-10 → Serial Monitor
  if (GamePad.isUpPressed()){
    digitalWrite(STRAIGHT_LEFT, HIGH);
    digitalWrite(STRAIGHT_RIGHT, HIGH);
    digitalWrite(BACK_LEFT, LOW);
    digitalWrite(BACK_RIGHT, LOW);
  }
  if (GamePad.isDownPressed()){
    digitalWrite(STRAIGHT_LEFT, LOW);
    digitalWrite(STRAIGHT_RIGHT, LOW);
    digitalWrite(BACK_LEFT, HIGH);
    digitalWrite(BACK_RIGHT, HIGH);
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
