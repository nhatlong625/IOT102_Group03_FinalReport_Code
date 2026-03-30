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
const int rainPin = 35;

const int relayPin = 13;
const int servoPin = 18;

const int ledYellow = 25;
const int ledRed = 26;
const int ledGreen = 27;

const int buzzer = 32;

const int buttonBarrier = 16;
const int buttonPump = 17;

// ===== SENSOR VALUE =====
int rainRaw;
int waterValue;
int floatState;
int rainValue;
int rainPercent;
int waterPercent;

// ===== TIMER =====
unsigned long lastBlink = 0;
unsigned long floatHigh = 0;
// ===== STATE =====
int servoState = -1;
int pumpState = -1;

bool ledState = false;
bool heavyRain = false;
bool risingWater = false;
bool manualBarrier = false;
bool manualPump = false;
bool pumpRunning = false;

bool isAutoMode = true;

// ===== MODE =====
void setMode(bool autoMode) {
  isAutoMode = autoMode;
  Blynk.virtualWrite(V5, isAutoMode ? 1 : 0);
  Serial.println(isAutoMode ? "AUTO MODE" : "MANUAL MODE");
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

// ===== LED BLINK =====
void blink(int led) {
  if (millis() - lastBlink > 250) {  // 250ms
    ledState = !ledState;
    digitalWrite(led, ledState);
    lastBlink = millis();
  }
}

// ===== MOVE BARRIER =====
void moveBarrier(int state) {
  if (state != servoState) {
    barrier.attach(servoPin);
    if (state == 1) {
      barrier.write(130);
      Blynk.virtualWrite(V4, 1);
    } else {
      barrier.write(0);
      Blynk.virtualWrite(V4, 0);
    }
    delay(200);
    barrier.detach();
    servoState = state;
  }
}

// ===== MOVE PUMP =====
void movePump(int state) {
  if (state != pumpState) {
    if (state == 1) {
      digitalWrite(relayPin, LOW);
      pumpRunning = true;
      Blynk.virtualWrite(V3, 1);
    } else {
      digitalWrite(relayPin, HIGH);
      pumpRunning = false;
      Blynk.virtualWrite(V3, 0);
    }
    pumpState = state;
  }
}

// ===== MANUAL MODE =====
void manualMode() {
  if (digitalRead(buttonBarrier) == LOW) {
    if (isAutoMode) setMode(false);

    manualBarrier = !manualBarrier;
    moveBarrier(manualBarrier ? 1 : 0);
    delay(300);
  }

  if (digitalRead(buttonPump) == LOW) {
    if (isAutoMode) setMode(false);

    manualPump = !manualPump;
    movePump(manualPump ? 1 : 0);
    delay(300);
  }
}
// ===== AUTO MODE =====
void autoMode() {

  if (waterPercent > 60) {
    if (!risingWater) {
      beep(5);
      Blynk.logEvent("flood", "Flood risk detected!");
    }
    risingWater = true;
  }

  if (waterPercent < 50) {
    risingWater = false;
  }

  if (rainPercent > 60) {
    if (!heavyRain) {
      beep(3);
      Blynk.logEvent("heavy_rain", "Heavy rain detected!");
    }
    heavyRain = true;
  }

  if (rainPercent < 50) {
    heavyRain = false;
  }
  // ===== FLOOD =====
  if (risingWater) {
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledYellow, LOW);
    blink(ledRed);

    if (!manualBarrier) moveBarrier(1);
  }

  // ===== HEAVY RAIN =====
  else if (heavyRain) {
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledRed, LOW);
    blink(ledYellow);
  }

  // ===== NORMAL =====
  else {
    digitalWrite(ledGreen, HIGH);
    digitalWrite(ledRed, LOW);
    digitalWrite(ledYellow, LOW);
    if (!manualBarrier) moveBarrier(0);
  }

  // ===== PUMP =====
  if (floatState == HIGH) {
    if (floatHigh == 0) floatHigh = millis();
    if (millis() - floatHigh > 3000) {
      if (!manualPump && pumpState != 1) {
        movePump(1);
        Blynk.logEvent("pump_on", "Pump started");
      }
    }
  } else {
    floatHigh = 0;
    if (!manualPump && pumpState != 0) {
      movePump(0);
      Blynk.logEvent("pump_off", "Pump stopped");
    }
  }
}

// ===== BLYNK CONTROL =====
BLYNK_WRITE(V3) {
  int value = param.asInt();

  if (isAutoMode) setMode(false);

  manualPump = value;
  movePump(value);
}

BLYNK_WRITE(V4) {
  int value = param.asInt();

  if (isAutoMode) setMode(false);

  manualBarrier = value;
  moveBarrier(value);
}

BLYNK_WRITE(V5) {
  int value = param.asInt();

  if (value == 1) {
    setMode(true);
    manualBarrier = false;
    manualPump = false;
  } else {
    setMode(false);
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);

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
  barrier.detach();

  digitalWrite(relayPin, HIGH);

  BlynkEdgent.begin();
}

// ===== LOOP =====
void loop() {
  BlynkEdgent.run();

  rainRaw = analogRead(rainPin);
  waterValue = analogRead(waterPin);
  floatState = digitalRead(floatPin);

  rainValue = max(0, 4095 - rainRaw);
  rainPercent = map(rainValue, 0, 4095, 0, 100);
  rainPercent = constrain(rainPercent, 0, 100);

  waterPercent = map(waterValue, 0, 2000, 0, 100);
  waterPercent = constrain(waterPercent, 0, 100);

  manualMode();

  Blynk.virtualWrite(V0, waterPercent);
  Blynk.virtualWrite(V1, rainPercent);
  Blynk.virtualWrite(V2, floatState);
  Blynk.virtualWrite(V5, isAutoMode ? 1 : 0);

  Serial.print("Rain: ");
  Serial.print(rainValue);
  Serial.print(" Water: ");
  Serial.print(waterValue);
  Serial.print(" Float: ");
  Serial.print(floatState);
  Serial.print(" Mode: ");
  Serial.println(isAutoMode ? "AUTO" : "MANUAL");

  if (isAutoMode) {
    autoMode();
  }
}
