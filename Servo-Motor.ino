#include <Servo.h>

Servo myServo;  // create servo object

void setup() {
  myServo.attach(9);  // attach servo signal to pin 9
}

void loop() {
  myServo.write(0);   // go to 0 degrees
  delay(1000);

  myServo.write(180); // go to 180 degrees
  delay(1000);
}