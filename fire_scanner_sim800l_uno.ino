#include <SoftwareSerial.h>
#include <Servo.h>

// ---------------- Pin configuration ----------------
const byte SERVO_1_PIN = 9;
const byte SERVO_2_PIN = 10;

const byte FLAME_1_PIN = 2;   // Digital output pin from flame sensor 1
const byte FLAME_2_PIN = 3;   // Digital output pin from flame sensor 2

const byte SIM800L_RX_PIN = 7; // Arduino RX  <- SIM800L TX
const byte SIM800L_TX_PIN = 8; // Arduino TX  -> SIM800L RX

// ---------------- User settings ----------------
const char PHONE_NUMBER[] = "+1234567890"; // Replace with your target number

const int SERVO_MIN_ANGLE = 10;
const int SERVO_MAX_ANGLE = 170; // ~160 degree sweep range (170-10)
const int SERVO_STEP = 2;
const unsigned long SERVO_STEP_INTERVAL_MS = 20;

const unsigned long FIRE_CLEAR_DELAY_MS = 1000; // Fire must stay gone this long before scanning resumes
const unsigned long CALL_DURATION_MS = 15000;   // Keep call active for 15 seconds, then hang up

// Flame modules are often ACTIVE LOW (LOW means fire).
// Change this to HIGH if your sensor output is active-high.
const bool FLAME_ACTIVE_LEVEL = LOW;

// ---------------- Globals ----------------
SoftwareSerial sim800l(SIM800L_RX_PIN, SIM800L_TX_PIN);
Servo servo1;
Servo servo2;

int servoAngle = SERVO_MIN_ANGLE;
int servoDirection = 1; // 1 = increasing angle, -1 = decreasing angle

unsigned long lastServoStepMs = 0;
unsigned long fireLastSeenMs = 0;

bool fireDetected = false;
bool alertSentForCurrentEvent = false;

// ---------------- SIM800L helpers ----------------
void sendAT(const char *cmd, unsigned long waitMs = 500) {
  sim800l.println(cmd);
  delay(waitMs);
  while (sim800l.available()) {
    Serial.write(sim800l.read());
  }
}

void sendSMS(const char *number, const char *message) {
  sendAT("AT+CMGF=1", 500); // SMS text mode

  sim800l.print("AT+CMGS=\"");
  sim800l.print(number);
  sim800l.println("\"");
  delay(500);

  sim800l.print(message);
  sim800l.write(26); // Ctrl+Z
  delay(5000);

  while (sim800l.available()) {
    Serial.write(sim800l.read());
  }
}

void makeCall(const char *number, unsigned long callDurationMs) {
  sim800l.print("ATD");
  sim800l.print(number);
  sim800l.println(";");

  unsigned long startMs = millis();
  while (millis() - startMs < callDurationMs) {
    while (sim800l.available()) {
      Serial.write(sim800l.read());
    }
  }

  sendAT("ATH", 500); // Hang up
}

void sendFireAlert() {
  sendSMS(PHONE_NUMBER, "WARNING: Flame detected by Arduino fire scanner!");
  makeCall(PHONE_NUMBER, CALL_DURATION_MS);
}

// ---------------- Fire + servo logic ----------------
bool isFlameDetectedNow() {
  int flame1 = digitalRead(FLAME_1_PIN);
  int flame2 = digitalRead(FLAME_2_PIN);

  return (flame1 == FLAME_ACTIVE_LEVEL) || (flame2 == FLAME_ACTIVE_LEVEL);
}

void updateServoScan() {
  unsigned long now = millis();
  if (now - lastServoStepMs < SERVO_STEP_INTERVAL_MS) {
    return;
  }
  lastServoStepMs = now;

  servoAngle += SERVO_STEP * servoDirection;

  if (servoAngle >= SERVO_MAX_ANGLE) {
    servoAngle = SERVO_MAX_ANGLE;
    servoDirection = -1;
  } else if (servoAngle <= SERVO_MIN_ANGLE) {
    servoAngle = SERVO_MIN_ANGLE;
    servoDirection = 1;
  }

  servo1.write(servoAngle);
  servo2.write(servoAngle);
}

void setup() {
  Serial.begin(9600);
  sim800l.begin(9600);

  pinMode(FLAME_1_PIN, INPUT);
  pinMode(FLAME_2_PIN, INPUT);

  servo1.attach(SERVO_1_PIN);
  servo2.attach(SERVO_2_PIN);
  servo1.write(servoAngle);
  servo2.write(servoAngle);

  delay(2000);

  // Basic SIM800L init
  sendAT("AT");
  sendAT("AT+CLIP=1");
}

void loop() {
  bool flameNow = isFlameDetectedNow();
  unsigned long now = millis();

  if (flameNow) {
    fireLastSeenMs = now;

    if (!fireDetected) {
      fireDetected = true;
      Serial.println("Flame detected! Stopping servos and sending alert.");
    }

    // Freeze servos at current position by not updating their angle.
    if (!alertSentForCurrentEvent) {
      alertSentForCurrentEvent = true;
      sendFireAlert();
    }
  } else {
    if (fireDetected && (now - fireLastSeenMs >= FIRE_CLEAR_DELAY_MS)) {
      fireDetected = false;
      alertSentForCurrentEvent = false;
      Serial.println("Flame cleared. Resuming scan.");
    }

    if (!fireDetected) {
      updateServoScan();
    }
  }
}
