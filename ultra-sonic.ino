// -------- Pin Definitions --------
#define TRIG1 12
#define ECHO1 11

#define TRIG2 9
#define ECHO2 10

#define TRIG3 8
#define ECHO3 7

// -------- Function to Read One Sensor --------
long readDistance(int trigPin, int echoPin) {
  long duration, distance;

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH, 30000); // timeout = 30ms

  distance = duration * 0.034 / 2; // convert to cm
  return distance;
}

void setup() {
  Serial.begin(9600);

  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);

  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);

  pinMode(TRIG3, OUTPUT);
  pinMode(ECHO3, INPUT);
}

void loop() {
  long d1 = readDistance(TRIG1, ECHO1);
  delay(60);  // critical gap to avoid cross-talk

  long d2 = readDistance(TRIG2, ECHO2);
  delay(60);

  long d3 = readDistance(TRIG3, ECHO3);
  delay(60);

  Serial.print("Sensor 1: ");
  Serial.print(d1);
  Serial.print(" cm   ");

  Serial.print("Sensor 2: ");
  Serial.print(d2);
  Serial.print(" cm   ");

  Serial.print("Sensor 3: ");
  Serial.print(d3);
  Serial.println(" cm");

  delay(200);
}