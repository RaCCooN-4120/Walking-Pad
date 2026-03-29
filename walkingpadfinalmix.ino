#include <Arduino.h>

#include <Wire.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include "SparkFun_BNO08x_Arduino_Library.h"

BNO08x myIMU;

float yaw = 0.0;

BLECharacteristic *yawChar;
bool deviceConnected = false;

#define SERVICE_UUID "41f9c825-5011-4c53-97c0-9f4bad424b82"
#define CHARACTERISTIC_UUID "00719a6f-a9b5-46e2-8707-38cac990d4ea"

#define BNO08X_ADDR 0x4A

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
  }
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;

    Serial.println("Client disconnected, restarting advertising...");
    pServer->getAdvertising()->start();
  }
};

float getYawFromQuat() {
  float qw = myIMU.getQuatReal();
  float qx = myIMU.getQuatI();
  float qy = myIMU.getQuatJ();
  float qz = myIMU.getQuatK();

  float ysqr = qy * qy;
  float t3 = +2.0f * (qw * qz + qx * qy);
  float t4 = +1.0f - 2.0f * (ysqr + qz * qz);
  float yawDeg = atan2(t3, t4) * 180.0f / PI;

  return yawDeg;
}

/* ===== MOTOR ===== */
const int pwmL = 25;
const int dirL = 26;
const int pwmR = 27;
const int dirR = 14;

/* ===== BUTTON ===== */
const int btnFwd = 32;
const int btnRev = 33;
unsigned long lastFwdPress = 0;
unsigned long lastRevPress = 0;
const unsigned long BTN_HOLD_TIME = 200;

/* ===== HALL ===== */
const int hallMain = 34;

// ====== CONFIG ======
const unsigned long rampTime = 1100;
const int fwdStart = 132;
const int fwdTarget = 105;
const int revStart = 131;
const int revTarget = 105;

// ====== STATE ======
int currentSpeed = 0;
unsigned long rampStartTime = 0;
int rampFrom = 0;
int rampTo = 0;
bool rampActive = false;

volatile long pulseMain = 0;
volatile unsigned long lastHallTime = 0;

/* ===== PARAM ===== */
const int PULSE_PER_REV = 20;
const float WHEEL_RADIUS = 0.0826;
const float BASE_WIDTH = 0.7;
const float PI_VAL = 3.1415926;

/* ===== ISR ===== */
void IRAM_ATTR hall_ISR() {
  unsigned long now = micros();

  if (now - lastHallTime > 5000) { // debounce 5ms
    pulseMain++;
    lastHallTime = now;
  }
}

/* ===== ลู่วิ่ง ===== */
const int sensorPin = 12;
const float SPEED_SCALE = 3.16;

volatile long pulseCount = 0;
volatile unsigned long lastInterruptTime = 0;

const float rollerRadius = 0.06;
float circumference;
float totalDistance = 0;

void IRAM_ATTR countPulse() {
  unsigned long now = micros();

  if (now - lastInterruptTime > 10000) {
    pulseCount++;
    lastInterruptTime = now;
  }
}

// ====== RAMP FUNCTION ======

void startRamp(int fromSpeed, int toSpeed) {
  rampFrom = fromSpeed;
  rampTo = toSpeed;
  rampStartTime = millis();
  rampActive = true;
}

void updateRamp() {

  if (!rampActive || currentSpeed == rampTo) return;

  unsigned long elapsed = millis() - rampStartTime;

  if (elapsed >= rampTime) {
    currentSpeed = rampTo;
    rampActive = false;
  }
  else {
    float progress = (float)elapsed / rampTime;
    currentSpeed = rampFrom + (rampTo - rampFrom) * progress;
  }
}

void setup() {

  Serial.begin(115200);
  while (!Serial) delay(10);
  Wire.begin();

  if (myIMU.begin(BNO08X_ADDR, Wire) == false) {
    Serial.println("BNO08x not detected at 0x4A. Check wiring!");
    while (1);
  }

  // ✅ Use Rotation Vector (magnetometer included)
  myIMU.enableRotationVector(50);

  // --- ส่วน Setup ---
  pinMode(pwmL, OUTPUT);
  pinMode(dirL, OUTPUT);
  pinMode(pwmR, OUTPUT);
  pinMode(dirR, OUTPUT);

  pinMode(btnFwd, INPUT_PULLUP);
  pinMode(btnRev, INPUT_PULLUP);

  pinMode(hallMain, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(hallMain), hall_ISR, FALLING);

  pinMode(sensorPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(sensorPin), countPulse, FALLING);

  circumference = 2 * PI_VAL * rollerRadius;

  BLEDevice::init("ESP32YawSensorTest");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  yawChar = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_READ
  );
  yawChar->addDescriptor(new BLE2902());
  pService->start();

  pServer->getAdvertising()->start();
  Serial.println("BLE advertising...");
  Serial.print("ESP32 BLE MAC Address: ");
  Serial.println(BLEDevice::getAddress().toString().c_str());
}

void loop() {
  if (myIMU.wasReset()) {
    Serial.println("BNO08x reset! Re-enabling Rotation Vector...");
    myIMU.enableRotationVector(50);
  }

  /* ===== CONTROL ===== */

  bool fwdRaw = (digitalRead(btnFwd) == HIGH);
  bool revRaw = (digitalRead(btnRev) == HIGH);

  if (fwdRaw) lastFwdPress = millis();
  if (revRaw) lastRevPress = millis();

  bool fwd = (millis() - lastFwdPress < BTN_HOLD_TIME);
  bool rev = (millis() - lastRevPress < BTN_HOLD_TIME);

  if (fwd && !rev) {

    if (!rampActive && currentSpeed != fwdTarget)
      startRamp(fwdStart, fwdTarget);

    digitalWrite(dirL, HIGH);
    digitalWrite(dirR, HIGH);

    updateRamp();
    analogWrite(pwmL, currentSpeed);
    analogWrite(pwmR, currentSpeed);
  }

  else if (rev && !fwd) {

    if (!rampActive && currentSpeed != revTarget)
      startRamp(revStart, revTarget);

    digitalWrite(dirL, LOW);
    digitalWrite(dirR, LOW);

    updateRamp();
    analogWrite(pwmL, currentSpeed);
    analogWrite(pwmR, currentSpeed);
  }

  else {

    rampTo = 0;
    rampActive = false;
    currentSpeed = 0;
    rampStartTime = 0;

    analogWrite(pwmL, 0);
    analogWrite(pwmR, 0);
  }


  /* ===== CALCULATE, AVERAGE & OUTPUT ===== */

  static unsigned long lastTime = 0;
  static long lastPulse = 0;
  static long lastPulseTread = 0;
  static long pulseAccum = 0;
  static unsigned long treadTimer = 0;

  static float speed = 0;

  // --- ตัวแปรสำหรับหาค่าเฉลี่ย ---
  static float omegaBuffer[5] = {0, 0, 0, 0, 0};
  static int omegaIdx = 0;

  static float speedBuffer[5] = {0, 0, 0, 0, 0};
  static int speedIdx = 0;

  if (millis() - lastTime >= 200) {

    long dP = pulseMain - lastPulse;

    float rpm = (dP * 60.0) / (PULSE_PER_REV * 0.2);
    float v = (2 * PI_VAL * WHEEL_RADIUS * rpm) / 60.0;

    float currentOmegaRaw = 0;

    if (v > 0.001) {
      if (fwd)
        currentOmegaRaw = v / BASE_WIDTH;
      else if (rev)
        currentOmegaRaw = -v / BASE_WIDTH;
    }

    omegaBuffer[omegaIdx] = currentOmegaRaw;
    omegaIdx = (omegaIdx + 1) % 5;

    float avgOmega =
      (omegaBuffer[0] +
       omegaBuffer[1] +
       omegaBuffer[2] +
       omegaBuffer[3] +
       omegaBuffer[4]) / 5.0;

    long dPulse = pulseCount - lastPulseTread;
    pulseAccum += dPulse;

    if (millis() - treadTimer >= 200) {
//ตัวเดิม//
      //float rawSpeed = pulseAccum * circumference;
      //rawSpeed = rawSpeed + 0.82;
      float rawSpeed = pulseAccum * circumference * SPEED_SCALE;
      totalDistance += rawSpeed;

      speedBuffer[speedIdx] = rawSpeed;
      speedIdx = (speedIdx + 1) % 3;

      speed =
        (speedBuffer[0] +
         speedBuffer[1] +
         speedBuffer[2]) / 3.0;

      Serial.println(speed);
      pulseAccum = 0;
      treadTimer = millis();
    }

    if (myIMU.getSensorEvent()) {
      yaw = getYawFromQuat();

      // Wrap -180..180
      if (yaw > 180) yaw -= 360;
      if (yaw < -180) yaw += 360;

      if (deviceConnected) {
        char buffer[32];
        char yawStr[10];
        char speedStr[10];

        dtostrf(yaw, 6, 2, yawStr);
        dtostrf(speed, 6, 2, speedStr);

        // รวมเป็น "yaw_speed"
        sprintf(buffer, "%s_%s", yawStr, speedStr);

        yawChar->setValue((uint8_t*)buffer, strlen(buffer));
        yawChar->notify();

        Serial.print("Connected-Yaw-Speed: ");
        Serial.println(buffer);
      }
      else {
        Serial.print("Yaw: ");
        Serial.println(yaw);
      }
    }

    lastPulse = pulseMain;
    lastPulseTread = pulseCount;
    lastTime = millis();
    delay(20);
  }
}