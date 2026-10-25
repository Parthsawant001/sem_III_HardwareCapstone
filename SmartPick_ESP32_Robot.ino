#include <WiFi.h>
#include <WebServer.h>

// =====================================================
// WiFi AP SETTINGS
// =====================================================
const char* apSsid = "ESP32_Car";
const char* apPassword = "12345678";

WebServer server(80);

// =====================================================
// L298N MOTOR DRIVER PINS
// =====================================================
#define ENA 25
#define ENB 33
#define IN1 26
#define IN2 27
#define IN3 14
#define IN4 12

// =====================================================
// HC-SR04 ULTRASONIC SENSOR
// =====================================================
#define TRIG_PIN 5
#define ECHO_PIN 18

// =====================================================
// SERVO GRIPPER
// =====================================================
#define SERVO_PIN 13

// Servo PWM settings
#define SERVO_FREQ 50
#define SERVO_RESOLUTION 16

// Gripper angles
#define GRIPPER_OPEN 40
#define GRIPPER_CLOSE 110

// =====================================================
// SPEED SETTINGS
// =====================================================
// Manual = 80% (80% of 255 = 204)
#define MANUAL_SPEED 204

// Automatic = 75% (75% of 255 = 191)
#define AUTO_SPEED 191

// =====================================================
// OBSTACLE DISTANCE
// =====================================================
#define OBSTACLE_DISTANCE 10

// =====================================================
// AUTOMATIC TIMINGS
// =====================================================
// Reverse for 400 ms
const unsigned long BACK_TIME = 400;

// Turn left for 600 ms
const unsigned long TURN_TIME = 600;

// =====================================================
// MOTOR FUNCTIONS
// =====================================================
void setMotorSpeed(uint8_t leftSpeed, uint8_t rightSpeed) {
  ledcWrite(ENA, leftSpeed);
  ledcWrite(ENB, rightSpeed);
}

void driveStop() {
  ledcWrite(ENA, 0);
  ledcWrite(ENB, 0);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void driveForward(uint8_t speed) {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  setMotorSpeed(speed, speed);
}

void driveBackward(uint8_t speed) {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  setMotorSpeed(speed, speed);
}

void driveLeft(uint8_t speed) {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  setMotorSpeed(speed, speed);
}

void driveRight(uint8_t speed) {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  setMotorSpeed(speed, speed);
}

// =====================================================
// ULTRASONIC DISTANCE
// =====================================================
long getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  // No echo timeout (max distance)
  if (duration == 0) {
    return 400;
  }

  long distance = duration * 0.0343 / 2;
  return distance;
}

// =====================================================
// SERVO CONTROL WITHOUT ESP32SERVO LIBRARY
// =====================================================
void servoWriteAngle(int angle) {
  if (angle < 0) {
    angle = 0;
  }
  if (angle > 180) {
    angle = 180;
  }

  // Convert angle to pulse width
  // 0 degree = 500 us, 180 degree = 2400 us
  int pulseWidth = map(angle, 0, 180, 500, 2400);

  // 50 Hz = 20,000 us period, 16-bit PWM = 0 to 65535
  uint32_t duty = ((uint32_t)pulseWidth * 65535UL) / 20000UL;
  ledcWrite(SERVO_PIN, duty);
}

void openGripper() {
  servoWriteAngle(GRIPPER_OPEN);
  Serial.println("GRIPPER OPEN");
}

void closeGripper() {
  servoWriteAngle(GRIPPER_CLOSE);
  Serial.println("GRIPPER CLOSED");
}

// =====================================================
// MANUAL CONTROL FLAGS
// =====================================================
bool forwardCmd = false;
bool backCmd = false;
bool leftCmd = false;
bool rightCmd = false;

// =====================================================
// MODE
// =====================================================
bool obstacleMode = false;

// =====================================================
// AUTOMATIC STATE MACHINE
// =====================================================
enum AutoState {
  AUTO_IDLE,
  AUTO_FORWARD,
  AUTO_BACKWARD,
  AUTO_TURN
};

AutoState autoState = AUTO_IDLE;
unsigned long autoStateStart = 0;

// =====================================================
// WEB PAGE HTML/CSS/JS (EMBEDDED IN PROGMEM)
// =====================================================
const char index_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>ESP32 Robot Car</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body {
  font-family: Arial, sans-serif;
  text-align: center;
  background: #111;
  color: white;
}
h1 { margin-top: 15px; }
h2 { margin-top: 30px; }
.btn {
  display: inline-block;
  padding: 18px 25px;
  margin: 7px;
  font-size: 18px;
  border-radius: 10px;
  border: none;
  min-width: 110px;
  background: #333;
  color: white;
}
.btn:active { transform: scale(0.97); }
.open { background: #1976d2; }
.close { background: #d32f2f; }
.modeOn { background: #1b5e20; }
.modeOff { background: #b71c1c; }
</style>
</head>
<body>
<h1>ESP32 Robot Car</h1>
<p>
Manual Speed: 80%<br>
Automatic Speed: 75%
</p>

<!-- CAR CONTROL -->
<h2>Car Control</h2>
<div>
  <button class="btn"
    onmousedown="sendCmd('f',1)" onmouseup="sendCmd('f',0)"
    ontouchstart="sendCmd('f',1)" ontouchend="sendCmd('f',0)">
    ▲ Forward
  </button>
</div>
<div>
  <button class="btn"
    onmousedown="sendCmd('l',1)" onmouseup="sendCmd('l',0)"
    ontouchstart="sendCmd('l',1)" ontouchend="sendCmd('l',0)">
    ◀ Left
  </button>
  <button class="btn" onclick="sendCmd('s',1)">
    ■ Stop
  </button>
  <button class="btn"
    onmousedown="sendCmd('r',1)" onmouseup="sendCmd('r',0)"
    ontouchstart="sendCmd('r',1)" ontouchend="sendCmd('r',0)">
    ▶ Right
  </button>
</div>
<div>
  <button class="btn"
    onmousedown="sendCmd('b',1)" onmouseup="sendCmd('b',0)"
    ontouchstart="sendCmd('b',1)" ontouchend="sendCmd('b',0)">
    ▼ Backward
  </button>
</div>

<!-- OBSTACLE MODE -->
<h2>Obstacle Avoiding</h2>
<p>Forward → Obstacle → Backward → Left → Forward</p>
<button class="btn modeOn" onclick="setMode(1)">AUTO ON</button>
<button class="btn modeOff" onclick="setMode(0)">AUTO OFF</button>

<!-- GRIPPER -->
<h2>Object Gripper</h2>
<button class="btn open" onclick="gripper('open')">OPEN GRIPPER</button>
<button class="btn close" onclick="gripper('close')">CLOSE GRIPPER</button>

<script>
function sendCmd(dir, state) {
  fetch(`/cmd?dir=${dir}&state=${state}`).catch(e => console.log(e));
}
function setMode(auto) {
  fetch(`/mode?auto=${auto}`).catch(e => console.log(e));
}
function gripper(action) {
  fetch(`/gripper?action=${action}`).catch(e => console.log(e));
}
</script>
</body>
</html>
)=====";

// =====================================================
// MANUAL MOVEMENT
// =====================================================
void handleManualMovement() {
  if (forwardCmd) {
    driveForward(MANUAL_SPEED);
  } else if (backCmd) {
    driveBackward(MANUAL_SPEED);
  } else if (leftCmd) {
    driveLeft(MANUAL_SPEED);
  } else if (rightCmd) {
    driveRight(MANUAL_SPEED);
  } else {
    driveStop();
  }
}

// =====================================================
// AUTOMATIC OBSTACLE AVOIDANCE
// =====================================================
void handleAutoObstacle() {
  unsigned long now = millis();

  switch (autoState) {
    case AUTO_FORWARD: {
      long distance = getDistance();
      Serial.print("Distance: ");
      Serial.print(distance);
      Serial.println(" cm");

      if (distance <= OBSTACLE_DISTANCE) {
        driveStop();
        autoState = AUTO_BACKWARD;
        autoStateStart = now;
      } else {
        driveForward(AUTO_SPEED);
      }
      break;
    }

    case AUTO_BACKWARD: {
      driveBackward(AUTO_SPEED);
      if (now - autoStateStart >= BACK_TIME) {
        driveStop();
        autoState = AUTO_TURN;
        autoStateStart = now;
      }
      break;
    }

    case AUTO_TURN: {
      driveLeft(AUTO_SPEED);
      if (now - autoStateStart >= TURN_TIME) {
        driveStop();
        autoState = AUTO_FORWARD;
        autoStateStart = now;
      }
      break;
    }

    case AUTO_IDLE: {
      driveStop();
      autoState = AUTO_FORWARD;
      autoStateStart = now;
      break;
    }
  }
}

// =====================================================
// WEB SERVER HANDLERS
// =====================================================
void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleCmd() {
  if (!server.hasArg("dir") || !server.hasArg("state")) {
    server.send(400, "text/plain", "Missing params");
    return;
  }

  String dir = server.arg("dir");
  int state = server.arg("state").toInt();

  if (!obstacleMode) {
    bool pressed = (state != 0);

    if (dir == "f") {
      forwardCmd = pressed;
      if (pressed) { backCmd = false; leftCmd = false; rightCmd = false; }
    } else if (dir == "b") {
      backCmd = pressed;
      if (pressed) { forwardCmd = false; leftCmd = false; rightCmd = false; }
    } else if (dir == "l") {
      leftCmd = pressed;
      if (pressed) { forwardCmd = false; backCmd = false; rightCmd = false; }
    } else if (dir == "r") {
      rightCmd = pressed;
      if (pressed) { forwardCmd = false; backCmd = false; leftCmd = false; }
    } else if (dir == "s") {
      forwardCmd = false;
      backCmd = false;
      leftCmd = false;
      rightCmd = false;
      driveStop();
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleMode() {
  if (!server.hasArg("auto")) {
    server.send(400, "text/plain", "Missing auto param");
    return;
  }

  int autoVal = server.arg("auto").toInt();
  if (autoVal == 1) {
    obstacleMode = true;
    forwardCmd = false;
    backCmd = false;
    leftCmd = false;
    rightCmd = false;
    autoState = AUTO_FORWARD;
    autoStateStart = millis();
  } else {
    obstacleMode = false;
    autoState = AUTO_IDLE;
    driveStop();
  }
  server.send(200, "text/plain", "Mode set");
}

void handleGripper() {
  if (!server.hasArg("action")) {
    server.send(400, "text/plain", "Missing action");
    return;
  }

  String action = server.arg("action");
  if (action == "open") {
    openGripper();
  } else if (action == "close") {
    closeGripper();
  } else {
    server.send(400, "text/plain", "Invalid action");
    return;
  }
  server.send(200, "text/plain", "Gripper OK");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);

  // Motor directional pins
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // Motor speed PWM pins (LEDC)
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);
  ledcAttach(ENA, 5000, 8);
  ledcAttach(ENB, 5000, 8);
  driveStop();

  // Ultrasonic sensor pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Servo setup (LEDC 50Hz, 16-bit)
  ledcAttach(SERVO_PIN, SERVO_FREQ, SERVO_RESOLUTION);
  openGripper();

  // Wi-Fi Access Point setup
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, apPassword);

  Serial.println();
  Serial.println("==============================");
  Serial.println("ESP32 ROBOT CAR");
  Serial.println("==============================");
  Serial.print("SSID: ");
  Serial.println(apSsid);
  Serial.print("Password: ");
  Serial.println(apPassword);
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());

  // Web server routing
  server.on("/", handleRoot);
  server.on("/cmd", handleCmd);
  server.on("/mode", handleMode);
  server.on("/gripper", handleGripper);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

// =====================================================
// MAIN LOOP
// =====================================================
void loop() {
  server.handleClient();

  if (obstacleMode) {
    handleAutoObstacle();
  } else {
    handleManualMovement();
  }

  delay(5);
}
