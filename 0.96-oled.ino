/*
 * 0.96-oled.ino  —  OLED Eye Expression Tester
 * =============================================
 * Dual 0.96" SSD1306 OLEDs (I2C), one eye per display.
 * Uses U8G2 _1_ page-buffer mode → only 128 bytes RAM per display.
 * Arduino Uno friendly (~40-50 % flash, ~45 % SRAM estimated).
 *
 * One drawEye() function, called with a mirror flag for each OLED.
 *   Left  OLED  →  I2C addr 0x3D  (eyeL)
 *   Right OLED  →  I2C addr 0x3C  (eyeR)
 *
 * Serial commands (9600 baud):
 *   n / N   Normal  (idle + blink)
 *   l / L   Look Left   (smooth gaze left then back)
 *   r / R   Look Right  (smooth gaze right then back)
 *   w / W   Look Down   (smooth gaze down then back)
 *   a / A   Angry   (diagonal brow cut — mirrored per eye)
 *   h / H   Happy   (bottom disc cut  — Akno style)
 *   d / D   Demo    (auto-cycle all expressions)
 *
 * Expression approach adapted from:
 *   AbdulsalamAbbod/Akno  (expressions.h — drawRBox + setDrawColor(0) overlays)
 *   circuitdigest.com     (Arduino OLED Eyes Animation comparison article)
 *   overall-code.ino      (smooth blink & page-buffer rendering from this project)
 */

#include <Wire.h>
#include <U8g2lib.h>

// ═══════════════════════════════════════════════
//  DISPLAYS  — _1_ page-buffer (128 B each)
// ═══════════════════════════════════════════════
U8G2_SSD1306_128X64_NONAME_1_HW_I2C eyeL(U8G2_R0, U8X8_PIN_NONE);
U8G2_SSD1306_128X64_NONAME_1_HW_I2C eyeR(U8G2_R0, U8X8_PIN_NONE);

// ═══════════════════════════════════════════════
//  EYE GEOMETRY  — single eye centred on 128×64
// ═══════════════════════════════════════════════
#define EYE_W   50      // base width
#define EYE_H   35      // base height
#define EYE_R    9      // corner radius
#define EYE_CX  64      // centre X of display
#define EYE_CY  30      // centre Y (slightly above mid for a "face" feel)

// ═══════════════════════════════════════════════
//  EXPRESSION STATE
// ═══════════════════════════════════════════════
enum Expr : uint8_t { NORMAL, ANGRY, HAPPY };
Expr    curExpr  = NORMAL;
uint8_t exprProg = 0;       // 0–100  animation progress
bool    exprDone = false;

// ═══════════════════════════════════════════════
//  GAZE / LOOK STATE  (smooth position offsets)
// ═══════════════════════════════════════════════
float gazeX     = 0;        // current X offset  (pixels)
float gazeY     = 0;        // current Y offset  (pixels)
float gazeTargX = 0;        // target  X
float gazeTargY = 0;        // target  Y
#define GAZE_SPEED  0.25f   // lerp factor per frame
#define GAZE_SHIFT  30      // max px shift L/R
#define GAZE_DOWN   14      // max px shift down

// Auto-return after a gaze command
bool          gazeReturn   = false;
unsigned long gazeHoldTime  = 0;
const unsigned long GAZE_HOLD = 600;  // ms to hold at target before returning

// ═══════════════════════════════════════════════
//  BLINK STATE  (only active in NORMAL + centre)
// ═══════════════════════════════════════════════
bool          blinkOn    = false;
uint8_t       blinkFrame = 0;
unsigned long lastBlink  = 0;

// ═══════════════════════════════════════════════
//  DEMO STATE
// ═══════════════════════════════════════════════
bool          demoOn   = false;
unsigned long demoT    = 0;
uint8_t       demoStep = 0;


// ─────────────────────────────────────────────────
//  drawEye()  — called inside firstPage/nextPage
//  isLeft = true  → left  eye (angry brow on RIGHT)
//  isLeft = false → right eye (angry brow on LEFT)
// ─────────────────────────────────────────────────
void drawEye(U8G2 &d, bool isLeft)
{
  uint8_t ew = EYE_W;
  uint8_t eh = EYE_H;
  int8_t  yOff = 0;

  // ── Expression tweaks on base shape ──────────
  if (curExpr == ANGRY) {
    // squint: shorter eye, nudged down like Akno (y=18,h=29 vs normal y=12,h=35)
    uint8_t sq = (uint8_t)map(exprProg, 0, 100, 0, 6);
    eh  -= sq;
    yOff = 3;
  }

  // ── Blink: smooth height compression ─────────
  uint8_t blinkRed = 0;
  if (blinkOn && blinkFrame < 7) {
    static const uint8_t bc[7] = {0, 6, 16, 32, 16, 6, 0};
    blinkRed = bc[blinkFrame];
  }
  if (blinkRed >= eh) blinkRed = eh - 3;
  eh -= blinkRed;

  // ── Apply gaze offset ────────────────────────
  int gx = (int)gazeX;   // same offset for both eyes = coordinated look
  int gy = (int)gazeY;

  // ── Top-left corner from centre + gaze ───────
  int ex = EYE_CX - (int)ew / 2 + gx;
  int ey = EYE_CY - (int)eh / 2 + yOff + blinkRed / 2 + gy;

  // ── Clamp radius so drawRBox doesn't assert ──
  uint8_t r = EYE_R;
  if (r > ew / 2 - 1) r = ew / 2 - 1;
  if (r > eh / 2 - 1) r = eh / 2 - 1;
  if (r < 1) r = 1;

  // ── 1) Draw filled rounded rectangle (the eye) ──
  d.drawRBox(ex, ey, ew, eh, r);

  // ── 2) ANGRY overlay — mirrored diagonal brow triangles ──
  //  Both eyes get the SAME angry brow shape but mirrored:
  //  Left  eye: brow slopes down toward the nose (right side deeper)
  //  Right eye: brow slopes down toward the nose (left  side deeper)
  //  Akno original: drawTriangle(3,14, 64,18+i, 124,14) on single screen
  //  We split & mirror that per-eye on separate OLEDs.
  if (curExpr == ANGRY && exprProg > 0) {
    uint8_t depth = (uint8_t)map(exprProg, 0, 100, 0, 15);
    d.setDrawColor(0);

    if (isLeft) {
      // Left eye: outer edge (left) stays flat, inner edge (right) dips down
      //   top-left corner flat, top-right corner sinks by 'depth'
      d.drawTriangle(
        ex - 5,        ey - 10,       // far above outer (left) edge
        ex + ew + 5,   ey - 10,       // far above inner (right) edge
        ex + ew + 5,   ey + depth     // inner edge drops ↓ by depth
      );
    } else {
      // Right eye: inner edge (left) dips down, outer edge (right) stays flat
      //   mirror of left eye
      d.drawTriangle(
        ex - 5,        ey - 10,       // far above inner (left) edge
        ex + ew + 5,   ey - 10,       // far above outer (right) edge
        ex - 5,        ey + depth     // inner edge drops ↓ by depth
      );
    }

    d.setDrawColor(1);
  }

  // ── 3) HAPPY overlay — large disc cutout from bottom ──
  if (curExpr == HAPPY && exprProg > 0) {
    uint8_t rise = (uint8_t)map(exprProg, 0, 100, 0, 4);
    d.setDrawColor(0);
    d.drawDisc(ex + ew / 2, ey + eh + 22 - rise, 38, U8G2_DRAW_ALL);
    d.setDrawColor(1);
  }
}


// ─────────────────────────────────────────────
//  renderEyes()  — redraw both OLEDs via page loop
// ─────────────────────────────────────────────
void renderEyes()
{
  eyeL.firstPage();
  do { drawEye(eyeL, true);  } while (eyeL.nextPage());

  eyeR.firstPage();
  do { drawEye(eyeR, false); } while (eyeR.nextPage());
}


// ─────────────────────────────────────────────
//  Animation helpers
// ─────────────────────────────────────────────
void updateExpr()
{
  if (!exprDone && exprProg < 100) {
    exprProg += 15;                 // ~7 frames to finish at 30 ms/frame
    if (exprProg > 100) exprProg = 100;
  }
  if (exprProg >= 100) exprDone = true;
}

void updateBlink()
{
  // Only blink when NORMAL and eyes are roughly centred
  if (curExpr != NORMAL) { blinkOn = false; return; }
  bool centred = (abs(gazeX) < 3 && abs(gazeY) < 3);
  if (!centred) return;

  if (!blinkOn && millis() - lastBlink > (unsigned long)random(3000, 6000)) {
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

// ─────────────────────────────────────────────
//  GAZE movement — smooth lerp toward target,
//  auto-return to centre after GAZE_HOLD ms
// ─────────────────────────────────────────────
void gazeSetTarget(float tx, float ty)
{
  gazeTargX    = tx;
  gazeTargY    = ty;
  gazeReturn   = false;
  gazeHoldTime = 0;
}

void lookLeft()  { gazeSetTarget(-GAZE_SHIFT, 0); }
void lookRight() { gazeSetTarget( GAZE_SHIFT, 0); }
void lookDown()  { gazeSetTarget(0, GAZE_DOWN);    }

void updateGaze()
{
  // Lerp toward target
  gazeX += (gazeTargX - gazeX) * GAZE_SPEED;
  gazeY += (gazeTargY - gazeY) * GAZE_SPEED;

  // When close enough to target and not yet returning, start hold timer
  bool atTarget = (abs(gazeTargX - gazeX) < 1.0f && abs(gazeTargY - gazeY) < 1.0f);
  if (atTarget && (gazeTargX != 0 || gazeTargY != 0)) {
    if (!gazeReturn) {
      if (gazeHoldTime == 0) gazeHoldTime = millis();
      if (millis() - gazeHoldTime > GAZE_HOLD) {
        gazeReturn = true;
        gazeTargX  = 0;
        gazeTargY  = 0;
      }
    }
  }
}

void setExpr(Expr e)
{
  if (curExpr == e) return;
  curExpr  = e;
  exprProg = 0;
  exprDone = false;
  blinkOn  = false;
  blinkFrame = 0;
  // Reset gaze when switching expressions
  gazeTargX = 0; gazeTargY = 0;
}

void updateDemo()
{
  if (!demoOn) return;
  if (millis() - demoT > 3000) {
    demoT = millis();
    demoStep = (demoStep + 1) % 6;
    switch (demoStep) {
      case 0: setExpr(NORMAL);                  break; // idle + blink
      case 1: setExpr(NORMAL); lookLeft();       break; // look left
      case 2: setExpr(NORMAL); lookRight();      break; // look right
      case 3: setExpr(NORMAL); lookDown();       break; // look down
      case 4: setExpr(ANGRY);                   break;
      case 5: setExpr(HAPPY);                   break;
    }
  }
}


// ═══════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════
void setup()
{
  Serial.begin(9600);
  Wire.begin();
  Wire.setClock(400000);        // fast-mode I2C

  delay(200);

  eyeL.setI2CAddress(0x3D * 2); // left  OLED
  eyeR.setI2CAddress(0x3C * 2); // right OLED
  eyeL.begin();
  eyeR.begin();

  Serial.println(F("=== OLED Eye Test ==="));
  Serial.println(F("n=Normal l=Left r=Right w=Down"));
  Serial.println(F("a=Angry  h=Happy  d=Demo"));

  lastBlink = millis();
}


// ═══════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════
void loop()
{
  // ── Serial command parser ──
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'n': case 'N':
        demoOn = false; setExpr(NORMAL);
        Serial.println(F("-> Normal")); break;
      case 'l': case 'L':
        demoOn = false; setExpr(NORMAL); lookLeft();
        Serial.println(F("-> Left"));   break;
      case 'r': case 'R':
        demoOn = false; setExpr(NORMAL); lookRight();
        Serial.println(F("-> Right"));  break;
      case 'w': case 'W':
        demoOn = false; setExpr(NORMAL); lookDown();
        Serial.println(F("-> Down"));   break;
      case 'a': case 'A':
        demoOn = false; setExpr(ANGRY);
        Serial.println(F("-> Angry"));  break;
      case 'h': case 'H':
        demoOn = false; setExpr(HAPPY);
        Serial.println(F("-> Happy"));  break;
      case 'd': case 'D':
        demoOn = true; demoT = millis(); demoStep = 0;
        setExpr(NORMAL);
        Serial.println(F("-> Demo"));   break;
    }
  }

  updateDemo();
  updateGaze();
  updateExpr();
  updateBlink();
  renderEyes();

  delay(30);                     // ~33 fps target per eye
}
