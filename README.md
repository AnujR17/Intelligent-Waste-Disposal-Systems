# Intelligent Waste Disposal System

An Arduino-based smart dustbin that detects humans and waste using ultrasonic sensors, provides audio feedback, and uses expressive OLED eyes to communicate its state — encouraging proper waste disposal through interactive behaviour.

## Files

### `overall-code.ino` — Main Production Code
The complete integrated firmware for the Intelligent Waste Disposal System.

**Hardware:**
- Arduino Uno
- 2× SSD1306 0.96" OLED displays (I2C) — left `0x3D`, right `0x3C`
- 3× HC-SR04 ultrasonic sensors (left, centre, right)
- 1× Servo motor (dustbin lid)
- DY-SV-5W MP3 audio module

**Eye System:**
- U8G2 `_1_` page-buffer mode — only 128 bytes RAM per OLED (Uno friendly)
- One `drawEye()` function with `isLeft` flag renders mirrored eyes on each OLED
- Expressions: **NORMAL** (idle blink), **ANGRY** (diagonal brow cut), **HAPPY** (smile arc)
- Smooth gaze: eyes track objects left/right/down via lerp interpolation

**Sensor → Eye Mapping:**
| Sensor | Condition | Eye Response |
|--------|-----------|--------------|
| Left ultrasonic | 2–30 cm | Eyes look left |
| Right ultrasonic | 2–30 cm | Eyes look right |
| Centre ultrasonic | 2–20 cm | Eyes look down (squished) |
| Centre ultrasonic | 20–40 cm | Human detected → servo opens lid |

**State Machine Flow:**
1. **WAITING** → human detected (centre 20–40 cm) → open lid
2. **OBSERVE 5s** → check for trash
3. No trash → play thank-you audio (00003) + **HAPPY eyes 5s**
4. Trash found → play warning audio (00001) → wait 7s
5. **CLEANUP CHECK 5s** → re-check
6. Cleaned up → play cleared audio (00002) + **HAPPY eyes 5s**
7. Still dirty → **ANGRY eyes 5s**

**Pin Assignments:**
| Component | Pins |
|-----------|------|
| Left ultrasonic | TRIG 12, ECHO 11 |
| Centre ultrasonic | TRIG 10, ECHO 9 |
| Right ultrasonic | TRIG 8, ECHO 7 |
| Audio WARN (00001) | Pin 5 |
| Audio CLEAR (00002) | Pin 3 |
| Audio THANK (00003) | Pin 2 |
| Servo | Pin 6 |
| OLEDs | I2C (A4/A5) |

---

### `0.96-oled.ino` — OLED Eye Test Sketch
Standalone test sketch for the eye animation system **without** sensors, servo, or audio. Useful for tuning eye expressions on the bench.

**Serial Commands (9600 baud):**
| Key | Action |
|-----|--------|
| `n` | Normal (idle + blink) |
| `l` | Look Left (smooth, auto-return) |
| `r` | Look Right (smooth, auto-return) |
| `w` | Look Down (smooth, auto-return) |
| `a` | Angry (mirrored diagonal brow) |
| `h` | Happy (smile arc cutout) |
| `d` | Demo (auto-cycle all expressions) |

---

## Libraries Required
- [U8g2](https://github.com/olikraus/u8g2) — OLED driver
- Wire (built-in) — I2C
- Servo (built-in)

## Performance Notes
- Ultrasonic delays reduced to 5 ms (from 50 ms) for smoother eye animation
- Estimated loop time ~70–80 ms → ~13–15 FPS
- Flash: ~51%, SRAM: ~55% on Arduino Uno

## References
- [Akno by AbdulsalamAbbod](https://github.com/AbdulsalamAbbod/Akno) — angry/happy expression technique (drawRBox + setDrawColor overlay)
- [Circuit Digest — Arduino OLED Eyes](https://circuitdigest.com/microcontroller-projects/arduino-oled-eyes-animation-for-robotics-projects) — comparison of eye animation approaches
