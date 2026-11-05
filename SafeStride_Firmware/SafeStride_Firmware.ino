/*
 * =====================================================================
 * PROJECT: SafeStride Rehabilitation Trainer
 * MODULE: PRODUCTION REHABILITATION FIRMWARE
 * CONTROLLER: Arduino Uno R3 (ATmega328P, 5V, 16 MHz)
 * TEAM: Nexora | Department of Engineering | Bennett University (ENGG101)
 * =====================================================================
 * 
 * CLINICAL SPECIFICATIONS & ESCALATION ARCHITECTURE:
 *   - Target Limit: 5.0 kg (default), adjustable up to 19.5 kg via buttons.
 *   - SAFE ZONE (< Limit - 2.0 kg): LED OFF, Buzzer SILENT.
 *   - STAGE 1 (CAUTION, Limit - 2.0 kg): STABLE (SOLID) RED LED ON, Buzzer SILENT.
 *   - STAGE 2 (WARNING, Limit - 1.0 kg): BLINKING RED LED + PULSING BUZZER.
 *   - STAGE 3 (OVERLOAD, >= Limit): BLINKING RED LED + FULL CONTINUOUS BUZZER ALARM.
 * 
 * HARDWARE PIN MAPPING:
 *   A0  : Heel Load Sensor (Row 12 right)
 *   A1  : Forefoot Load Sensor (Row 12 left)
 *   D2  : LCD RS
 *   D3  : LCD E
 *   D4  : LCD DB7 (Pin 14)
 *   D5  : LCD DB6 (Pin 13)
 *   D6  : LCD DB5 (Pin 12)
 *   D7  : LCD DB4 (Pin 11)
 *   D8  : Button OY (+1.0 kg step)
 *   D9  : Button BR (+0.5 kg step)
 *   D10 : Active/Passive Piezo Buzzer
 *   D11 : Caution Red LED (via 220 Ohm resistor)
 * =====================================================================
 */

#include <LiquidCrystal.h>

// --- PIN ASSIGNMENTS ---
const int PIN_LCD_RS = 2;
const int PIN_LCD_EN = 3;
const int PIN_LCD_D4 = 7; // LCD Pin 11 (DB4)
const int PIN_LCD_D5 = 6; // LCD Pin 12 (DB5)
const int PIN_LCD_D6 = 5; // LCD Pin 13 (DB6)
const int PIN_LCD_D7 = 4; // LCD Pin 14 (DB7)

const int PIN_FSR_HEEL     = A0;
const int PIN_FSR_FOREFOOT = A1;

const int PIN_BTN_OY = 8; // +1.0 kg increment
const int PIN_BTN_BR = 9; // +0.5 kg increment

const int PIN_BUZZER = 10;
const int PIN_LED    = 11;

// --- INITIALIZE LCD (16x2 Parallel) ---
LiquidCrystal lcd(PIN_LCD_RS, PIN_LCD_EN, PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7);

// --- UNIVERSAL BUZZER CONTROLLER ---
void buzzerSound(bool on, int freq = 2600) {
  if (on) {
    tone(PIN_BUZZER, freq);
    digitalWrite(PIN_BUZZER, HIGH);
  } else {
    noTone(PIN_BUZZER);
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// --- CALIBRATED PIECEWISE TRANSFER FUNCTION (FINGER & INSOLE TUNED) ---
float adcToKg(int adcVal) {
  if (adcVal < 80) return 0.0; // Deadband noise floor

  // Smooth realistic hand & insole force curve:
  if (adcVal <= 500) {
    // 80 to 500 maps to 0.0 to 1.2 kg (light touch)
    return (float)(adcVal - 80) * (1.2 / 420.0);
  } else if (adcVal <= 750) {
    // 500 to 750 maps to 1.2 to 3.0 kg (moderate press)
    return 1.2 + (float)(adcVal - 500) * (1.8 / 250.0);
  } else if (adcVal <= 880) {
    // 750 to 880 maps to 3.0 to 5.5 kg (firm press)
    return 3.0 + (float)(adcVal - 750) * (2.5 / 130.0);
  } else if (adcVal <= 960) {
    // 880 to 960 maps to 5.5 to 12.0 kg (strong body load)
    return 5.5 + (float)(adcVal - 880) * (6.5 / 80.0);
  } else {
    // 960 to 1023 maps to 12.0 to 19.5 kg (maximum limit ceiling)
    float kg = 12.0 + (float)(adcVal - 960) * (7.5 / 63.0);
    if (kg > 19.5) kg = 19.5;
    return kg;
  }
}

// --- SYSTEM LIMITS ---
float targetLimit = 5.0; // Default evaluation limit: 5.0 kg
const float CAUTION_MARGIN = 2.0; // 2 kg diff: Stable Solid LED
const float WARNING_MARGIN = 1.0; // 1 kg diff: Pulsing Buzzer + Blinking LED

// --- BUTTON DEBOUNCE TRACKING ---
int lastBtnOY = HIGH;
int lastBtnBR = HIGH;
unsigned long lastDebounce = 0;

// --- TIMING INTERVALS ---
unsigned long prevLoopMillis = 0;
unsigned long prevLcdMillis = 0;
unsigned long prevSerialMillis = 0;

// --- SMOOTHED RUNTIME METRICS ---
float smoothedWeight = 0.0;
float smoothedHeel = 0.0;
float smoothedForefoot = 0.0;

void setup() {
  Serial.begin(9600);

  // Configure Outputs
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  buzzerSound(false);
  digitalWrite(PIN_LED, LOW);

  // Configure Buttons with Internal Pullups
  pinMode(PIN_BTN_OY, INPUT_PULLUP);
  pinMode(PIN_BTN_BR, INPUT_PULLUP);

  // Initialize LCD Screen
  lcd.begin(16, 2);
  lcd.clear();

  // Professional Startup Chirp (100 ms)
  digitalWrite(PIN_LED, HIGH);
  buzzerSound(true, 2800);
  delay(120);
  buzzerSound(false);
  digitalWrite(PIN_LED, LOW);

  // Official Welcome Screen
  lcd.setCursor(0, 0);
  lcd.print("SafeStride Live ");
  lcd.setCursor(0, 1);
  lcd.print("Nexora BU | Init");
  delay(900);
  lcd.clear();

  Serial.println("==================================================================");
  Serial.println("SAFESTRIDE REHABILITATION TRAINER - OFFICIAL PRODUCTION FIRMWARE");
  Serial.println("Team Nexora | Department of Engineering | Bennett University");
  Serial.println("Default Limit: 5.0 kg (Ceiling: 19.5 kg) | 3-Stage Biofeedback Ready");
  Serial.println("==================================================================");
}

void loop() {
  unsigned long currentMillis = millis();

  // -----------------------------------------------------------------
  // 1. BUTTON CONTROLS (Live Limit Adjustments)
  // -----------------------------------------------------------------
  int btnOY = digitalRead(PIN_BTN_OY);
  int btnBR = digitalRead(PIN_BTN_BR);

  // Button OY: Add +1.0 kg
  if (btnOY == LOW && lastBtnOY == HIGH && (currentMillis - lastDebounce > 220)) {
    targetLimit += 1.0;
    if (targetLimit > 19.5) targetLimit = 3.5;
    lastDebounce = currentMillis;

    // Audible button feedback
    buzzerSound(true, 3000); delay(40); buzzerSound(false);

    Serial.print(">>> [BUTTON OY] NEW TARGET LIMIT: ");
    Serial.print(targetLimit, 1);
    Serial.print(" kg | Caution@: ");
    Serial.print(targetLimit - CAUTION_MARGIN, 1);
    Serial.print(" kg | Warning@: ");
    Serial.print(targetLimit - WARNING_MARGIN, 1);
    Serial.println(" kg <<<");
  }

  // Button BR: Add +0.5 kg
  if (btnBR == LOW && lastBtnBR == HIGH && (currentMillis - lastDebounce > 220)) {
    targetLimit += 0.5;
    if (targetLimit > 19.5) targetLimit = 3.5;
    lastDebounce = currentMillis;

    // Audible button feedback
    buzzerSound(true, 3500); delay(40); buzzerSound(false);

    Serial.print(">>> [BUTTON BR] NEW TARGET LIMIT: ");
    Serial.print(targetLimit, 1);
    Serial.print(" kg | Caution@: ");
    Serial.print(targetLimit - CAUTION_MARGIN, 1);
    Serial.print(" kg | Warning@: ");
    Serial.print(targetLimit - WARNING_MARGIN, 1);
    Serial.println(" kg <<<");
  }

  lastBtnOY = btnOY;
  lastBtnBR = btnBR;

  // -----------------------------------------------------------------
  // 2. 50 Hz BARE-METAL LOAD SENSING & 3-STAGE BIOFEEDBACK
  // -----------------------------------------------------------------
  static const char* systemState = "SAFE     ";

  if (currentMillis - prevLoopMillis >= 20) {
    prevLoopMillis = currentMillis;

    // Read analog foot sensors
    int rawHeel     = analogRead(PIN_FSR_HEEL);
    int rawForefoot = analogRead(PIN_FSR_FOREFOOT);

    float rawWeightHeel = adcToKg(rawHeel);
    float rawWeightFore = adcToKg(rawForefoot);

    // EMA Low-Pass Filter (prevents jitter, delivers rock-solid numbers)
    smoothedHeel     = 0.65 * rawWeightHeel + 0.35 * smoothedHeel;
    smoothedForefoot = 0.65 * rawWeightFore + 0.35 * smoothedForefoot;
    smoothedWeight   = smoothedHeel + smoothedForefoot;
    if (smoothedWeight < 0.15) smoothedWeight = 0.0;

    float cautionThreshold = targetLimit - CAUTION_MARGIN; // e.g. 3.0 kg
    float warningThreshold = targetLimit - WARNING_MARGIN; // e.g. 4.0 kg

    // 5 Hz non-blocking blink strobe (100 ms ON / 100 ms OFF)
    bool blinkPulse = ((currentMillis / 100) % 2 == 0);

    // STAGE 3: OVERLOAD REACHED (>= Target Limit)
    // Behavior: Blinking Red LED + Full-on Continuous 85 dB Buzzer
    if (smoothedWeight >= targetLimit) {
      systemState = "OVERLOAD!";
      digitalWrite(PIN_LED, blinkPulse ? HIGH : LOW);
      buzzerSound(true, 2800); // Continuous loud alarm
    }
    // STAGE 2: WARNING (>= Limit - 1.0 kg && < Limit)
    // Behavior: Blinking Red LED + Pulsing Synchronous Buzzer
    else if (smoothedWeight >= warningThreshold) {
      systemState = "WARNING  ";
      digitalWrite(PIN_LED, blinkPulse ? HIGH : LOW);
      buzzerSound(blinkPulse, 2400); // Pulsing warning beep
    }
    // STAGE 1: CAUTION (>= Limit - 2.0 kg && < Limit - 1.0 kg)
    // Behavior: Stable Solid Red LED + Buzzer Silent
    else if (smoothedWeight >= cautionThreshold) {
      systemState = "CAUTION  ";
      digitalWrite(PIN_LED, HIGH);   // Solid ON
      buzzerSound(false);            // Silent
    }
    // SAFE ZONE (< Limit - 2.0 kg)
    else {
      systemState = "SAFE     ";
      digitalWrite(PIN_LED, LOW);
      buzzerSound(false);
    }
  }

  // -----------------------------------------------------------------
  // 3. LCD REAL-TIME SCREEN REFRESH (150 ms)
  // -----------------------------------------------------------------
  if (currentMillis - prevLcdMillis >= 150) {
    prevLcdMillis = currentMillis;

    char row0[17];
    char row1[17];

    // Row 0: "Live:  X.X kg   "
    int twWhole = (int)smoothedWeight;
    int twFrac  = (int)((smoothedWeight - twWhole) * 10 + 0.5);
    if (twFrac >= 10) { twWhole++; twFrac = 0; }
    snprintf(row0, sizeof(row0), "Live: %2d.%1d kg   ", twWhole, twFrac);

    // Row 1: "L: 5.0k SAFE    " / "L: 5.0k CAUTION " / "L: 5.0k OVERLOAD!"
    int tlWhole = (int)targetLimit;
    int tlFrac  = (int)((targetLimit - tlWhole) * 10 + 0.5);
    if (tlFrac >= 10) { tlWhole++; tlFrac = 0; }
    snprintf(row1, sizeof(row1), "L:%2d.%1d %-9s", tlWhole, tlFrac, systemState);

    lcd.setCursor(0, 0);
    lcd.print(row0);
    lcd.setCursor(0, 1);
    lcd.print(row1);
  }

  // -----------------------------------------------------------------
  // 4. PRODUCTION SERIAL TELEMETRY (250 ms)
  // -----------------------------------------------------------------
  if (currentMillis - prevSerialMillis >= 250) {
    prevSerialMillis = currentMillis;

    Serial.print("[SAFESTRIDE] Load: ");
    if (smoothedWeight < 10.0) Serial.print(" ");
    Serial.print(smoothedWeight, 1);
    Serial.print(" kg | Target: ");
    Serial.print(targetLimit, 1);
    Serial.print(" kg | Status: ");
    Serial.print(systemState);
    Serial.print(" | Heel: ");
    Serial.print(smoothedHeel, 1);
    Serial.print(" kg | Fore: ");
    Serial.print(smoothedForefoot, 1);
    Serial.println(" kg");
  }
}
