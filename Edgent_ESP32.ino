#define BLYNK_TEMPLATE_ID "TMPL6-v9U3j-_"
#define BLYNK_TEMPLATE_NAME "IoT102 Project"
#define BLYNK_FIRMWARE_VERSION "0.1.0"

#define BLYNK_PRINT Serial
#define APP_DEBUG
#define USE_ESP32_DEV_MODULE

#include <WiFi.h>
#include <ESP32Servo.h>
#include "BlynkEdgent.h"

Servo barrier;

// ===== PIN =====
const int floatPin = 33;
const int waterPin = 34;
const int rainPin  = 35;

const int relayPin = 13;
const int servoPin = 18;

const int ledYellow = 25;
const int ledRed    = 26;
const int ledGreen  = 27;

const int buzzer = 32;

const int buttonBarrier = 16;
const int buttonPump    = 17;

// ===== THRESHOLD =====
const int rainThreshold  = 2500;
const int floodThreshold = 1000;

// ===== SENSOR VALUE =====
int rainValue  = 0;
int waterValue = 0;
int floatState = 0;

// ===== TIMER =====
unsigned long lastBlink = 0;
unsigned long lastManualAction = 0;
unsigned long lastSend = 0;
unsigned long lastManualWarning = 0;

const unsigned long manualTimeout = 5000;
const unsigned long sendInterval = 1000;
const unsigned long warningCooldown = 3000;

// ===== STATE =====
int servoState = 0;   // 0 = CLOSE, 1 = OPEN
int pumpState  = 0;   // 0 = OFF,   1 = ON

bool ledState         = false;
bool heavyRain        = false;
bool risingWater      = false;
bool pumpRunning      = false;
bool isAutoMode       = true;
bool manualLocked     = false;
bool manualWarningSent = false;

// ===== HELPER =====
bool isDangerousCondition() {
  return (waterValue > floodThreshold || floatState == HIGH || rainValue < rainThreshold);
}

// ===== BUZZER =====
void beep(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(buzzer, HIGH);
    delay(200);
    digitalWrite(buzzer, LOW);
    delay(200);
  }
}

// ===== WARNING =====
void warningBeforeOverride(const char* msg) {
  beep(2);
  Serial.println(msg);
  Blynk.logEvent("warning", msg);
  manualWarningSent = true;
  lastManualWarning = millis();
}

// ===== LED BLINK =====
void blink(int led) {
  if (millis() - lastBlink > 250) {
    ledState = !ledState;
    digitalWrite(led, ledState);
    lastBlink = millis();
  }
}

// ===== MODE =====
void setMode(bool modeValue) {
  isAutoMode = modeValue;
  Blynk.virtualWrite(V5, isAutoMode ? 1 : 0);
  Serial.println(isAutoMode ? "AUTO MODE" : "MANUAL MODE");
}

void switchToManual() {
  manualLocked = true;
  setMode(false);
  lastManualAction = millis();
}

void checkManualTimeout() {
  if (!isAutoMode && manualLocked) {
    if (millis() - lastManualAction >= manualTimeout) {
      manualLocked = false;
      setMode(true);
    }
  }
}

void checkManualSafetyTimeout() {
  if (manualWarningSent && millis() - lastManualWarning >= warningCooldown) {
    manualWarningSent = false;
  }
}

// ===== MOVE BARRIER =====
void moveBarrier(int state) {
  if (state != servoState) {
    barrier.attach(servoPin);

    if (state == 1) {
      barrier.write(130);   // OPEN
      Blynk.virtualWrite(V4, 1);
    } else {
      barrier.write(0);     // CLOSE
      Blynk.virtualWrite(V4, 0);
    }

    delay(300);
    barrier.detach();
    servoState = state;
  }
}

// ===== MOVE PUMP =====
void movePump(int state) {
  if (state != pumpState) {
    if (state == 1) {
      digitalWrite(relayPin, LOW);   // ON
      pumpRunning = true;
      Blynk.virtualWrite(V3, 1);
    } else {
      digitalWrite(relayPin, HIGH);  // OFF
      pumpRunning = false;
      Blynk.virtualWrite(V3, 0);
    }

    pumpState = state;
  }
}

// ===== PHYSICAL BUTTONS =====
void manualMode() {
  if (digitalRead(buttonBarrier) == LOW) {
    switchToManual();

    if (servoState == 1) {
      if (isDangerousCondition()) {
        warningBeforeOverride("Warning: Barrier is being CLOSED during dangerous condition!");
      }
      moveBarrier(0);
    } else {
      moveBarrier(1);
    }

    delay(300);
  }

  if (digitalRead(buttonPump) == LOW) {
    switchToManual();

    if (pumpState == 1) {
      if (isDangerousCondition()) {
        warningBeforeOverride("Warning: Pump is being TURNED OFF during dangerous condition!");
      }
      movePump(0);
    } else {
      movePump(1);
    }

    delay(300);
  }
}

// ===== AUTO MODE =====
void autoMode() {
  if (isAutoMode) {

    // PRIORITY 1: WATER HIGH
    if (waterValue > floodThreshold) {
      digitalWrite(ledGreen, LOW);
      digitalWrite(ledYellow, LOW);
      blink(ledRed);

      moveBarrier(1);

      if (!risingWater) {
        beep(5);
        risingWater = true;
        Blynk.logEvent("flood", "Flood risk detected!");
      }

      heavyRain = false;
    }

    // PRIORITY 2: HEAVY RAIN
    else if (rainValue < rainThreshold) {
      digitalWrite(ledGreen, LOW);
      digitalWrite(ledRed, LOW);
      blink(ledYellow);

      if (!heavyRain) {
        beep(3);
        heavyRain = true;
        Blynk.logEvent("heavy_rain", "Heavy rain detected!");
      }

      risingWater = false;
    }

    // PRIORITY 3: SAFE CONDITION
    else {
      digitalWrite(ledGreen, HIGH);
      digitalWrite(ledRed, LOW);
      digitalWrite(ledYellow, LOW);

      moveBarrier(0);

      heavyRain = false;
      risingWater = false;
    }

    // FLOAT SWITCH CONTROL PUMP
    if (floatState == HIGH) {
      if (!pumpRunning) {
        movePump(1);
        Blynk.logEvent("pump_on", "Pump started");
      }
    } else {
      if (pumpRunning) {
        movePump(0);
        Blynk.logEvent("pump_off", "Pump stopped");
      }
    }
  }
}

// ===== BLYNK CONTROL =====
BLYNK_WRITE(V3) {
  int value = param.asInt();

  switchToManual();

  if (value == 1) {
    movePump(1);
  } else {
    if (isDangerousCondition()) {
      warningBeforeOverride("Warning: Pump is being TURNED OFF from Blynk during dangerous condition!");
    }
    movePump(0);
  }
}

BLYNK_WRITE(V4) {
  int value = param.asInt();

  switchToManual();

  if (value == 1) {
    moveBarrier(1);
  } else {
    if (isDangerousCondition()) {
      warningBeforeOverride("Warning: Barrier is being CLOSED from Blynk during dangerous condition!");
    }
    moveBarrier(0);
  }
}

BLYNK_WRITE(V5) {
  int value = param.asInt();

  if (value == 1) {
    manualLocked = false;
    setMode(true);   // AUTO
  } else {
    manualLocked = true;
    setMode(false);  // MANUAL
    lastManualAction = millis();
  }
}

// ===== CONNECTED =====
BLYNK_CONNECTED() {
  Blynk.virtualWrite(V0, waterValue);
  Blynk.virtualWrite(V1, rainValue);
  Blynk.virtualWrite(V2, floatState);
  Blynk.virtualWrite(V3, pumpState);
  Blynk.virtualWrite(V4, servoState);
  Blynk.virtualWrite(V5, isAutoMode ? 1 : 0);
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(rainPin, INPUT);
  pinMode(waterPin, INPUT);
  pinMode(floatPin, INPUT_PULLUP);

  pinMode(buttonBarrier, INPUT_PULLUP);
  pinMode(buttonPump, INPUT_PULLUP);

  pinMode(relayPin, OUTPUT);

  pinMode(ledGreen, OUTPUT);
  pinMode(ledYellow, OUTPUT);
  pinMode(ledRed, OUTPUT);

  pinMode(buzzer, OUTPUT);

  barrier.attach(servoPin);
  barrier.write(0);
  delay(300);
  barrier.detach();

  digitalWrite(relayPin, HIGH);

  servoState = 0;
  pumpState = 0;
  pumpRunning = false;
  manualLocked = false;
  manualWarningSent = false;

  setMode(true);

  BlynkEdgent.begin();
}

// ===== LOOP =====
void loop() {
  BlynkEdgent.run();

  rainValue  = analogRead(rainPin);
  waterValue = analogRead(waterPin);
  floatState = digitalRead(floatPin);

  if (millis() - lastSend >= sendInterval) {
    lastSend = millis();

    Blynk.virtualWrite(V0, waterValue);
    Blynk.virtualWrite(V1, rainValue);
    Blynk.virtualWrite(V2, floatState);

    Serial.print("Rain: ");
    Serial.print(rainValue);
    Serial.print(" | Water: ");
    Serial.print(waterValue);
    Serial.print(" | Float: ");
    Serial.print(floatState);
    Serial.print(" | Barrier: ");
    Serial.print(servoState);
    Serial.print(" | Pump: ");
    Serial.print(pumpState);
    Serial.print(" | Mode: ");
    Serial.print(isAutoMode ? "AUTO" : "MANUAL");
    Serial.print(" | Dangerous: ");
    Serial.print(isDangerousCondition() ? "YES" : "NO");
    Serial.print(" | ManualWarningSent: ");
    Serial.println(manualWarningSent ? "YES" : "NO");
  }

  manualMode();
  checkManualTimeout();
  checkManualSafetyTimeout();
  autoMode();
}