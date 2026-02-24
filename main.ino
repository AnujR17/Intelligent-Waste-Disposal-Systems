/* ====================================================
 * Hardware :  Arduino Uno, 2× SSD1306 0.96" OLED (I2C),
 *             3× HC-SR04 ultrasonic, 1× servo, DY-SV-5W audio
 * OLEDs    :  U8G2 _1_ page-buffer (128 B RAM each)
 *             eyeL @ 0x3D  /  eyeR @ 0x3C
 *
 * Eye system (from 0.96-oled.ino):
 *   - Expressions : NORMAL (blink), ANGRY (mirrored brow), HAPPY (smile arc)
 *   - Gaze        : lookLeft / lookRight / lookDown  (smooth lerp, auto-return)
 *   - One drawEye() with isLeft flag → mirrored for each OLED
 *
 * Sensor-to-eye mapping:
 *   left  ultrasonic < 30 cm  → lookLeft()
 *   right ultrasonic < 30 cm  → lookRight()
 *   centre < 20 cm            → lookDown()   (object below)
 *   centre 20–40 cm           → human detected → servo / state machine
 *   AUDIO_THANK (00003) or AUDIO_CLEAR (00002) played → HAPPY 5 s
 *   Still dirty after cleanup  → ANGRY 5 s
 *   Otherwise                  → NORMAL (idle blink)
 */

#include <Wire.h>
#include <U8g2lib.h>
#include <Servo.h>

// ================= OLED EYES (_1_ page-buffer) =================
U8G2_SSD1306_128X64_NONAME_1_HW_I2C eyeL(U8G2_R0, U8X8_PIN_NONE);
U8G2_SSD1306_128X64_NONAME_1_HW_I2C eyeR(U8G2_R0, U8X8_PIN_NONE);

// Eye geometry — one eye centred on each 128×64 OLED
#define EYE_W   50
#define EYE_H   35
#define EYE_R    9
#define EYE_CX  64
#define EYE_CY  30

// ── Expression state ──
enum Expr : uint8_t { NORMAL, ANGRY, HAPPY };
Expr    curExpr  = NORMAL;
uint8_t exprProg = 0;        // 0–100 animation progress
bool    exprDone = false;

// Timed expression (angry / happy auto-revert to NORMAL)
bool          timedExpr      = false;
unsigned long timedExprStart = 0;
unsigned long timedExprLen   = 0;     // ms

// ── Gaze (smooth position offset) ──
float gazeX = 0, gazeY = 0;
float gazeTargX = 0, gazeTargY = 0;
#define GAZE_SPEED 0.18f    // slightly faster L/R travel
#define GAZE_SHIFT 38      // further L/R reach
#define GAZE_DOWN  14

// ── Blink (NORMAL + centred only) ──
bool          blinkOn    = false;
uint8_t       blinkFrame = 0;
unsigned long lastBlink  = 0;

// ================= SERVO =================
Servo head;

// ================= ULTRASONIC =================
#define L_TRIG 12
#define L_ECHO 11
#define C_TRIG 10
#define C_ECHO 9
#define R_TRIG 8
#define R_ECHO 7

// ================= AUDIO =================
#define AUDIO_WARN   5   // 00001
#define AUDIO_CLEAR  3   // 00002
#define AUDIO_THANK  2   // 00003

// ================= STATE MACHINE =================
enum InteractionState {
  WAITING,
  OBSERVE_5S,
  WARNING_PLAYING_7S,
  CLEANUP_CHECK_5S
};

InteractionState interaction = WAITING;
unsigned long stateStart = 0;
bool objectSeen = false;


// =============================================
//  DISTANCE
// =============================================
long readDistance(int trig, int echo){
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 30000);
  return dur * 0.034 / 2;
}

// =============================================
//  SOUND TRIGGER
// =============================================
void triggerSound(int pin){
  digitalWrite(pin, LOW);
  delay(200);
  digitalWrite(pin, HIGH);
}


// =============================================
//  DRAW EYE  — called inside firstPage/nextPage
//  isLeft = true  → left  OLED (angry brow inner = right)
//  isLeft = false → right OLED (angry brow inner = left)
// =============================================
void drawEye(U8G2 &d, bool isLeft)
{
  uint8_t ew = EYE_W;
  uint8_t eh = EYE_H;
  int8_t  yOff = 0;

  // ── ANGRY squint ──
  if (curExpr == ANGRY) {
    uint8_t sq = (uint8_t)map(exprProg, 0, 100, 0, 6);
    eh  -= sq;
    yOff = 3;
  }

  // ── Blink compression ──
  uint8_t blinkRed = 0;
  if (blinkOn && blinkFrame < 7) {
    static const uint8_t bc[7] = {0, 6, 16, 32, 16, 6, 0};
    blinkRed = bc[blinkFrame];
  }
  if (blinkRed >= eh) blinkRed = eh - 3;
  eh -= blinkRed;

  // ── Down-gaze squish: compress vertically when looking down ──
  if (gazeY > 2) {
    uint8_t squish = (uint8_t)((gazeY / GAZE_DOWN) * 8);  // up to 8 px shorter
    if (squish > eh - 6) squish = eh - 6;
    eh -= squish;
  }

  // ── Gaze offset (same for both eyes → coordinated look) ──
  int gx = (int)gazeX;
  int gy = (int)gazeY;

  int ex = EYE_CX - (int)ew / 2 + gx;
  int ey = EYE_CY - (int)eh / 2 + yOff + blinkRed / 2 + gy;

  // ── Clamp corner radius ──
  uint8_t r = EYE_R;
  if (r > ew / 2 - 1) r = ew / 2 - 1;
  if (r > eh / 2 - 1) r = eh / 2 - 1;
  if (r < 1) r = 1;

  // 1) Base rounded rect
  d.drawRBox(ex, ey, ew, eh, r);

  // 2) ANGRY overlay — mirrored diagonal brow triangles
  if (curExpr == ANGRY && exprProg > 0) {
    uint8_t depth = (uint8_t)map(exprProg, 0, 100, 0, 15);
    d.setDrawColor(0);
    if (isLeft) {
      // left eye: outer (left) flat, inner (right) dips
      d.drawTriangle(
        ex - 5,       ey - 10,
        ex + ew + 5,  ey - 10,
        ex + ew + 5,  ey + depth);
    } else {
      // right eye: inner (left) dips, outer (right) flat
      d.drawTriangle(
        ex - 5,       ey - 10,
        ex + ew + 5,  ey - 10,
        ex - 5,       ey + depth);
    }
    d.setDrawColor(1);
  }

  // 3) HAPPY overlay — disc cutout (smile arc)
  if (curExpr == HAPPY && exprProg > 0) {
    uint8_t rise = (uint8_t)map(exprProg, 0, 100, 0, 4);
    d.setDrawColor(0);
    d.drawDisc(ex + ew / 2, ey + eh + 22 - rise, 38, U8G2_DRAW_ALL);
    d.setDrawColor(1);
  }
}

void renderEyes(){
  eyeL.firstPage(); do { drawEye(eyeL, true);  } while (eyeL.nextPage());
  eyeR.firstPage(); do { drawEye(eyeR, false); } while (eyeR.nextPage());
}


// =============================================
//  EXPRESSION HELPERS
// =============================================
void setExpr(Expr e){
  if (curExpr == e) return;
  curExpr    = e;
  exprProg   = 0;
  exprDone   = false;
  blinkOn    = false;
  blinkFrame = 0;
}

// Set a timed expression that auto-reverts to NORMAL
void setTimedExpr(Expr e, unsigned long durationMs){
  setExpr(e);
  timedExpr      = true;
  timedExprStart = millis();
  timedExprLen   = durationMs;
}

void updateExpr(){
  // Animate expression ramp-in
  if (!exprDone && exprProg < 100) {
    exprProg += 15;
    if (exprProg > 100) exprProg = 100;
  }
  if (exprProg >= 100) exprDone = true;

  // Auto-revert timed expression
  if (timedExpr && millis() - timedExprStart > timedExprLen) {
    timedExpr = false;
    setExpr(NORMAL);
  }
}


// =============================================
//  BLINK  (only NORMAL + eyes roughly centred)
// =============================================
void updateBlink(){
  if (curExpr != NORMAL) { blinkOn = false; return; }
  // Blink regardless of gaze position — eyes blink while moving
  if (!blinkOn && millis() - lastBlink > (unsigned long)random(3000, 6000)){
    blinkOn    = true;
    blinkFrame = 0;
  }
  if (blinkOn) {
    blinkFrame++;
    if (blinkFrame >= 7) {
      blinkOn    = false;
      blinkFrame = 0;
      lastBlink  = millis();
    }
  }
}


// =============================================
//  GAZE  (smooth lerp, continuous tracking)
// =============================================
void updateGaze(){
  gazeX += (gazeTargX - gazeX) * GAZE_SPEED;
  gazeY += (gazeTargY - gazeY) * GAZE_SPEED;
}


// ================= SETUP =================
void setup(){
  Serial.begin(9600);
  Wire.begin();
  Wire.setClock(400000);

  delay(200);
  eyeL.setI2CAddress(0x3D * 2);
  eyeR.setI2CAddress(0x3C * 2);
  eyeL.begin();
  eyeR.begin();

  pinMode(L_TRIG, OUTPUT); pinMode(L_ECHO, INPUT);
  pinMode(C_TRIG, OUTPUT); pinMode(C_ECHO, INPUT);
  pinMode(R_TRIG, OUTPUT); pinMode(R_ECHO, INPUT);

  pinMode(AUDIO_WARN, OUTPUT);
  pinMode(AUDIO_CLEAR, OUTPUT);
  pinMode(AUDIO_THANK, OUTPUT);

  digitalWrite(AUDIO_WARN, HIGH);
  digitalWrite(AUDIO_CLEAR, HIGH);
  digitalWrite(AUDIO_THANK, HIGH);

  head.attach(6);
  head.write(90);

  lastBlink = millis();
}


// ================= LOOP =================
void loop(){

  // ── Read sensors ──
  long leftD   = readDistance(L_TRIG, L_ECHO);  delay(5);
  long centerD = readDistance(C_TRIG, C_ECHO);  delay(5);
  long rightD  = readDistance(R_TRIG, R_ECHO);

  Serial.print(F("L:")); Serial.print(leftD);
  Serial.print(F("  C:")); Serial.print(centerD);
  Serial.print(F("  R:")); Serial.println(rightD);

  bool humanDetected  = (centerD >= 20 && centerD <= 40);

  // ── Servo follow human ──
  if (humanDetected) head.write(180);
  else if (centerD > 80) head.write(0);

  // ================= INTERACTION STATE MACHINE =================
  switch (interaction) {

  // ── WAIT FOR HUMAN ──
  case WAITING:
    if (humanDetected) {
      head.write(180);                 // open lid
      interaction = OBSERVE_5S;
      stateStart  = millis();
      Serial.println(F("Human detected -> observing 5s"));
    }
    break;

  // ── FIRST 5 s OBSERVATION ──
  case OBSERVE_5S:
    if (millis() - stateStart > 5000) {

      bool clean =
        !(leftD  < 30 && leftD  > 2) &&
        !(rightD < 30 && rightD > 2) &&
        !(centerD < 20 && centerD > 2);

      if (clean) {
        triggerSound(AUDIO_THANK);     // 00003
        setTimedExpr(HAPPY, 5000);     // happy eyes for 5 s
        Serial.println(F("No trash -> play 00003 + happy"));
        interaction = WAITING;
      } else {
        triggerSound(AUDIO_WARN);      // 00001
        Serial.println(F("Trash detected -> play 00001"));
        interaction = WARNING_PLAYING_7S;
        stateStart  = millis();
      }
    }
    break;

  // ── WAIT FOR WARNING AUDIO (7 s) ──
  case WARNING_PLAYING_7S:
    if (millis() - stateStart > 7000) {
      interaction = CLEANUP_CHECK_5S;
      stateStart  = millis();
      Serial.println(F("Warning done -> give 5s to correct"));
    }
    break;

  // ── FINAL 5 s CLEANUP WINDOW ──
  case CLEANUP_CHECK_5S:
    if (millis() - stateStart > 5000) {

      bool clean =
        !(leftD  < 30 && leftD  > 2) &&
        !(rightD < 30 && rightD > 2) &&
        !(centerD < 20 && centerD > 2);

      if (clean) {
        triggerSound(AUDIO_CLEAR);     // 00002
        setTimedExpr(HAPPY, 5000);     // happy eyes for 5 s
        Serial.println(F("Corrected -> play 00002 + happy"));
      } else {
        // angry expression for 5 seconds
        setTimedExpr(ANGRY, 5000);
        Serial.println(F("Still dirty -> angry eyes 5s"));
      }

      head.write(0);                   // close lid again
      interaction = WAITING;
    }
    break;
  }

  // ================= GAZE TARGET (from sensors) =================
  // Only update gaze direction when NOT in a timed expression
  // (angry/happy hold their own centre-gaze)
  if (!timedExpr) {
    if (leftD > 2 && leftD < 30) {
      // Object on the left → eyes look left
      gazeTargX = -GAZE_SHIFT;  gazeTargY = 0;
    }
    else if (rightD > 2 && rightD < 30) {
      // Object on the right → eyes look right
      gazeTargX = GAZE_SHIFT;   gazeTargY = 0;
    }
    else if (centerD > 2 && centerD < 20) {
      // Object close below → eyes look down
      gazeTargX = 0;  gazeTargY = GAZE_DOWN;
    }
    else {
      // Nothing nearby → eyes centre (idle blink will kick in)
      gazeTargX = 0;  gazeTargY = 0;
    }

    // Make sure expression is NORMAL when no timed expr is active
    if (curExpr != NORMAL) setExpr(NORMAL);
  } else {
    // During timed expression keep gaze centred
    gazeTargX = 0;  gazeTargY = 0;
  }

  // ── Update all eye subsystems ──
  updateGaze();
  updateExpr();
  updateBlink();
  renderEyes();

  delay(5);
}