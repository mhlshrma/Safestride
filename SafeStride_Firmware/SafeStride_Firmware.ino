/*
 * =====================================================================
 * PROJECT: SafeStride Rehabilitation Trainer
 * CONTROLLER: Arduino Uno R3 (ATmega328P, 5V, 16 MHz)
 * TEAM: Nexora | ENGG101 Introduction to Engineering | Bennett University
 * =====================================================================
 * 
 * CALIBRATION & HARDWARE UPGRADES:
 *   1. Full RP-C30-ST Datasheet Conductance Calibration:
 *        G = 1 / Rs = ADC / (10k * (1023 - ADC))
 *        Weight (kg) = (G - 0.104) * 20.0 kg
 *        Corrects non-linear voltage divider curve.
 *   2. Universal Buzzer Driver:
 *        Uses tone(PIN_BUZZER, freq) + digitalWrite for compatibility
 *        with BOTH Active and Passive piezo buzzers.
 *   3. 500 ms Startup Self-Test Beep on boot.
 *   4. LCD Pin Configuration toggle (Straight vs Reversed data pins).
 * =====================================================================
 */

#include <LiquidCrystal.h>

// --- PIN DEFINITIONS ---
const int PIN_LCD_RS = 2;
const int PIN_LCD_EN = 3;

// Hardware Verified Pin Mapping:
// RS -> D2, EN -> D3
// LCD Pin 11 (DB4) -> Arduino D7
// LCD Pin 12 (DB5) -> Arduino D6
// LCD Pin 13 (DB6) -> Arduino D5
// LCD Pin 14 (DB7) -> Arduino D4
LiquidCrystal lcd(2, 3, 7, 6, 5, 4);

const int PIN_FSR_HEEL     = A0;
const int PIN_FSR_FOREFOOT = A1;

const int PIN_BTN_OY = 8; // Button OY: +1.0 kg step
const int PIN_BTN_BR = 9; // Button BR: +0.5 kg step

const int PIN_BUZZER = 10;
const int PIN_LED    = 11;

// --- SYSTEM STATE & LIMITS ---
bool systemArmed = true; // Auto-active on power-on

float targetLimit = 5.0;       // Default limit: 5.0 kg
const float MAX_LIMIT = 19.5;  // Absolute ceiling: 19.5 kg
const float MIN_LIMIT = 3.5;   // Minimum threshold

const float LIGHT_MARGIN = 3.0; // Light turns on 3.0 kg before limit (e.g. at 2.0 kg when limit is 5.0)
const float BUZZ_MARGIN  = 2.0; // Buzz warning starts 2.0 kg before limit (e.g. at 3.0 kg)
const float HYSTERESIS   = 0.20; // 0.20 kg deadband prevents boundary jitter/flicker

// --- BUTTON DEBOUNCE TRACKING ---
int lastBtnOYState = HIGH;
int lastBtnBRState = HIGH;
unsigned long lastDebounceMillis = 0;
const unsigned long DEBOUNCE_DELAY = 180; // ms

// --- TIMING INTERVALS ---
unsigned long prevLoopMillis = 0;
const unsigned long LOOP_INTERVAL = 20; // 50 Hz bare-metal polling

unsigned long prevLcdMillis = 0;
const unsigned long LCD_INTERVAL = 150; // LCD refresh (approx 6.6 Hz)

unsigned long prevSerialMillis = 0;
const unsigned long SERIAL_INTERVAL = 250; // Telemetry stream (4 Hz)

// =====================================================================
// UNIVERSAL BUZZER CONTROLLER (Works for Active & Passive buzzers)
// =====================================================================
void buzzerSound(bool on, int freq = 2500) {
  if (on) {
    tone(PIN_BUZZER, freq);
    digitalWrite(PIN_BUZZER, HIGH);
  } else {
    noTone(PIN_BUZZER);
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// =====================================================================
// CALIBRATED RP-C30-ST SENSOR TRANSFER FUNCTION
// =====================================================================
// Circuit: 5V supply, R0 = 10.0 kOhm fixed pull-down to GND, Rs = FSR to +5V
// Divider: Vout = 5.0 * 10 / (10 + Rs) => Rs = 10.0 * (1023 - ADC) / ADC
// Conductance: G = 1 / Rs = ADC / (10.0 * (1023 - ADC)) in 1/kOhm
// Datasheet: Conductance is linear with force: G = 5e-5 * F(g) + 0.104
// Force (kg) = (G - 0.104) * 20.0 kg
float adcToKg(int adcVal) {
  if (adcVal < 100) return 0.0; // Baseline no-load deadband
  if (adcVal >= 1000) adcVal = 1000; // Saturated clamp

  float g = (float)adcVal / (10.0 * (1023.0 - (float)adcVal));
  float forceKg = (g - 0.104) * 20.0;

  if (forceKg < 0.15) forceKg = 0.0;
  if (forceKg > 20.0) forceKg = 20.0;
  return forceKg;
}

float getResistanceKohm(int adcVal) {
  if (adcVal <= 5) return 999.0;
  return 10.0 * (1023.0 - (float)adcVal) / (float)adcVal;
}

void setup() {
  Serial.begin(9600);

  // Configure Output Pins
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED, LOW);

  // Configure Input Buttons with Internal Pullups
  pinMode(PIN_BTN_OY, INPUT_PULLUP);
  pinMode(PIN_BTN_BR, INPUT_PULLUP);

  // Initialize LCD Screen
  lcd.begin(16, 2);
  lcd.clear();

  // Startup Audible & Visual Self-Test (500 ms beep)
  digitalWrite(PIN_LED, HIGH);
  buzzerSound(true, 2500);
  delay(500);
  buzzerSound(false);
  digitalWrite(PIN_LED, LOW);

  // Welcome Screen
  lcd.setCursor(0, 0);
  lcd.print("SafeStride Live ");
  lcd.setCursor(0, 1);
  lcd.print("Limit: 5.0kg Set");
  delay(800);
  lcd.clear();

  Serial.println("==================================================================");
  Serial.println("SAFESTRIDE REHABILITATION TRAINER - CALIBRATED & ACTIVE");
  Serial.println("RP-C30-ST Datasheet Conductance Model Active: G = (ADC)/(10k*(1023-ADC))");
  Serial.println("Target Limit: 5.0 kg | Light @ 2.0 kg | Buzzer @ 3.0 kg");
  Serial.println("==================================================================");
}

void loop() {
  unsigned long currentMillis = millis();

  int btnOY = digitalRead(PIN_BTN_OY);
  int btnBR = digitalRead(PIN_BTN_BR);

  // 1. DUAL BUTTON TOGGLE (Pause / Resume)
  if (btnOY == LOW && btnBR == LOW) {
    if (currentMillis - lastDebounceMillis > 450) {
      systemArmed = !systemArmed;
      lastDebounceMillis = currentMillis;

      if (systemArmed) {
        buzzerSound(true, 2200); delay(60); buzzerSound(false);
        Serial.println("\n>>> [PROCESS] System Resumed: ACTIVE <<<");
      } else {
        buzzerSound(false);
        digitalWrite(PIN_LED, LOW);
        Serial.println("\n>>> [PROCESS] System Paused: STANDBY <<<");
      }
    }
  }
  // 2. TARGET LIMIT ADJUSTMENTS (Up to 19.5 kg)
  else if (systemArmed) {
    if (btnOY == LOW && lastBtnOYState == HIGH && (currentMillis - lastDebounceMillis > DEBOUNCE_DELAY)) {
      targetLimit += 1.0;
      if (targetLimit > MAX_LIMIT) targetLimit = MIN_LIMIT;
      lastDebounceMillis = currentMillis;
      Serial.print(">>> [BUTTON OY +1.0kg] NEW LIMIT: ");
      Serial.print(targetLimit, 1);
      Serial.print(" kg | LightAt: ");
      Serial.print(targetLimit - LIGHT_MARGIN, 1);
      Serial.print(" kg | BuzzAt: ");
      Serial.print(targetLimit - BUZZ_MARGIN, 1);
      Serial.println(" kg <<<");
    }

    if (btnBR == LOW && lastBtnBRState == HIGH && (currentMillis - lastDebounceMillis > DEBOUNCE_DELAY)) {
      targetLimit += 0.5;
      if (targetLimit > MAX_LIMIT) targetLimit = MIN_LIMIT;
      lastDebounceMillis = currentMillis;
      Serial.print(">>> [BUTTON BR +0.5kg] NEW LIMIT: ");
      Serial.print(targetLimit, 1);
      Serial.print(" kg | LightAt: ");
      Serial.print(targetLimit - LIGHT_MARGIN, 1);
      Serial.print(" kg | BuzzAt: ");
      Serial.print(targetLimit - BUZZ_MARGIN, 1);
      Serial.println(" kg <<<");
    }
  }

  lastBtnOYState = btnOY;
  lastBtnBRState = btnBR;

  // 3. 50 Hz BARE-METAL SENSING & MULTI-STAGE BIOFEEDBACK
  static int rawHeel = 0;
  static int rawForefoot = 0;
  static float filteredHeel = 0.0;
  static float filteredForefoot = 0.0;
  static float weightHeel = 0.0;
  static float weightForefoot = 0.0;
  static float totalWeight = 0.0;
  static const char* systemState = "SAFE     ";

  if (currentMillis - prevLoopMillis >= LOOP_INTERVAL) {
    prevLoopMillis = currentMillis;

    rawHeel     = analogRead(PIN_FSR_HEEL);
    rawForefoot = analogRead(PIN_FSR_FOREFOOT);

    // Exponential Moving Average filter to eliminate ADC voltage noise
    if (filteredHeel <= 0.01 && rawHeel > 0) filteredHeel = rawHeel;
    else filteredHeel = 0.75 * filteredHeel + 0.25 * (float)rawHeel;

    if (filteredForefoot <= 0.01 && rawForefoot > 0) filteredForefoot = rawForefoot;
    else filteredForefoot = 0.75 * filteredForefoot + 0.25 * (float)rawForefoot;

    weightHeel     = adcToKg((int)(filteredHeel + 0.5));
    weightForefoot = adcToKg((int)(filteredForefoot + 0.5));
    totalWeight    = weightHeel + weightForefoot;

    if (systemArmed) {
      float lightThreshold = targetLimit - LIGHT_MARGIN; // e.g. 2.0 kg
      float buzzThreshold  = targetLimit - BUZZ_MARGIN;  // e.g. 3.0 kg

      // State machine with Schmitt Trigger Hysteresis to eliminate boundary flickering
      if (strcmp(systemState, "OVERLOAD!") == 0) {
        if (totalWeight < (lightThreshold - HYSTERESIS)) {
          systemState = "SAFE     ";
        } else if (totalWeight < (buzzThreshold - HYSTERESIS)) {
          systemState = "CAUTION  ";
        } else if (totalWeight < (targetLimit - HYSTERESIS)) {
          systemState = "BUZZ WARN";
        }
      } else if (strcmp(systemState, "BUZZ WARN") == 0) {
        if (totalWeight >= targetLimit) {
          systemState = "OVERLOAD!";
        } else if (totalWeight < (lightThreshold - HYSTERESIS)) {
          systemState = "SAFE     ";
        } else if (totalWeight < (buzzThreshold - HYSTERESIS)) {
          systemState = "CAUTION  ";
        }
      } else if (strcmp(systemState, "CAUTION  ") == 0) {
        if (totalWeight >= targetLimit) {
          systemState = "OVERLOAD!";
        } else if (totalWeight >= buzzThreshold) {
          systemState = "BUZZ WARN";
        } else if (totalWeight < (lightThreshold - HYSTERESIS)) {
          systemState = "SAFE     ";
        }
      } else { // SAFE or initial
        if (totalWeight >= targetLimit) {
          systemState = "OVERLOAD!";
        } else if (totalWeight >= buzzThreshold) {
          systemState = "BUZZ WARN";
        } else if (totalWeight >= lightThreshold) {
          systemState = "CAUTION  ";
        } else {
          systemState = "SAFE     ";
        }
      }

      // Actuator Outputs based on stable hysteresis state
      if (strcmp(systemState, "OVERLOAD!") == 0) {
        digitalWrite(PIN_LED, HIGH);
        buzzerSound(true, 2800); // 2.8 kHz continuous alarm
      } else if (strcmp(systemState, "BUZZ WARN") == 0) {
        digitalWrite(PIN_LED, HIGH);
        bool beepPulse = ((currentMillis / 80) % 2 == 0);
        buzzerSound(beepPulse, 2200); // 2.2 kHz pulsing warning
      } else if (strcmp(systemState, "CAUTION  ") == 0) {
        digitalWrite(PIN_LED, HIGH);
        buzzerSound(false);
      } else {
        digitalWrite(PIN_LED, LOW);
        buzzerSound(false);
      }
    } else {
      systemState = "STANDBY  ";
      digitalWrite(PIN_LED, LOW);
      buzzerSound(false);
    }
  }

  // 4. SCREEN DISPLAY
  if (currentMillis - prevLcdMillis >= LCD_INTERVAL) {
    prevLcdMillis = currentMillis;

    char row0[17];
    char row1[17];

    if (systemArmed) {
      int twWhole = (int)totalWeight;
      int twFrac  = (int)((totalWeight - twWhole) * 10 + 0.5);
      if (twFrac >= 10) { twWhole++; twFrac = 0; }
      snprintf(row0, sizeof(row0), "Live: %2d.%1d kg   ", twWhole, twFrac);

      int tlWhole = (int)targetLimit;
      int tlFrac  = (int)((targetLimit - tlWhole) * 10 + 0.5);
      if (tlFrac >= 10) { tlWhole++; tlFrac = 0; }
      snprintf(row1, sizeof(row1), "L:%2d.%1d %-9s", tlWhole, tlFrac, systemState);
    } else {
      snprintf(row0, sizeof(row0), "SafeStride Pause");
      snprintf(row1, sizeof(row1), "Press OY+BR Start");
    }

    lcd.setCursor(0, 0);
    lcd.print(row0);
    lcd.setCursor(0, 1);
    lcd.print(row1);
  }

  // 5. SERIAL MONITOR STREAM (Detailed physics calibration output)
  if (currentMillis - prevSerialMillis >= SERIAL_INTERVAL) {
    prevSerialMillis = currentMillis;

    float lightThresh = targetLimit - LIGHT_MARGIN;
    float buzzThresh  = targetLimit - BUZZ_MARGIN;
    float heelR = getResistanceKohm(rawHeel);
    float foreR = getResistanceKohm(rawForefoot);

    Serial.print("[PROCESS] State: ");
    Serial.print(systemState);
    Serial.print(" | Weight: ");
    if (totalWeight < 10.0) Serial.print(" ");
    Serial.print(totalWeight, 1);
    Serial.print("kg | Lim: ");
    Serial.print(targetLimit, 1);
    Serial.print("kg | LightAt: ");
    Serial.print(lightThresh, 1);
    Serial.print("kg | BuzzAt: ");
    Serial.print(buzzThresh, 1);
    Serial.print("kg || [INPUTS] Heel(A0): ADC=");
    Serial.print(rawHeel);
    Serial.print(" (");
    Serial.print(heelR, 1);
    Serial.print("k, ");
    Serial.print(weightHeel, 1);
    Serial.print("kg) | Fore(A1): ADC=");
    Serial.print(rawForefoot);
    Serial.print(" (");
    Serial.print(foreR, 1);
    Serial.print("k, ");
    Serial.print(weightForefoot, 1);
    Serial.print("kg) | OY: ");
    Serial.print(btnOY);
    Serial.print(" | BR: ");
    Serial.println(btnBR);
  }
}
