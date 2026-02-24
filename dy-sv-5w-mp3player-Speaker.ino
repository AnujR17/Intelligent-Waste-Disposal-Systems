// ===== DY-SV MP3 Test Code =====
// IO0 -> Arduino 5 -> plays 00001.mp3
// IO1 -> Arduino 3 -> plays 00002.mp3
// IO2 -> Arduino 2 -> plays 00003.mp3

const int sound1 = 5;
const int sound2 = 3;
const int sound3 = 2;

void setup() {
  pinMode(sound1, OUTPUT);
  pinMode(sound2, OUTPUT);
  pinMode(sound3, OUTPUT);

  // Keep pins HIGH so nothing plays accidentally
  digitalWrite(sound1, HIGH);
  digitalWrite(sound2, HIGH);
  digitalWrite(sound3, HIGH);

  delay(6000);  // Let DY-SV fully boot (important)
}

void loop() {

  playSound(sound1);   // should play 0001.mp3
  delay(7000);         

  playSound(sound2);   // should play 0002.mp3
  delay(7000);

  playSound(sound3);   // should play 0003.mp3
  delay(7000);
}


// Function that triggers the module
void playSound(int pin) {
  digitalWrite(pin, LOW);   // Trigger (active LOW)
  delay(350);               // Longer pulse needed for IO1/IO2 reliability
  digitalWrite(pin, HIGH);  // Release trigger
}
