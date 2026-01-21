/*
  ESP8266 HC-SR04 (TRIG=D6, ECHO=D5) + MPU6050 → UDP broadcast (port 6666)
  - Wi-Fi: SSID "GXXR", PASS "1234567890gsxr" (auto-reconnect; restart on repeated failures)
  - Sonar math & timing: EXACTLY as given (0.343 mm/µs, divide by 2, pulseIn)
  - JSON: {"node":"FRONT-SS-IMU","ts":...,
           "sonar":{"echo_us":..., "mm":..., "cm":...} OR {"timeout":1},
           "imu":{"ax":...,"ay":...,"az":...,"gx":...,"gy":...,"gz":...,"t":...}}
  - Debug: set DEBUG=1 to print once per second

  NOTE: HC-SR04 ECHO is 5V — use a divider/level shifter for D5 (3.3V max).
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ------------------- Config -------------------
#define DEBUG 0

#define NODE_ID     "FRONT-SS-IMU"
const char* WIFI_SSID = "GXXR";
const char* WIFI_PASS = "1234567890gsxr";
const uint16_t UDP_PORT = 6666;

// Sonar pins (as per your latest defines)
#define TRIG_PIN  D6
#define ECHO_PIN  D5

// Your original sonar constants (unchanged)
const float    SOUND_SPEED_MM_PER_US = 0.343f;     // ~20°C
const uint32_t ECHO_TIMEOUT_US       = 30000UL;    // 30 ms ~ 5.1 m round trip

// I2C / MPU6050
#define SDA_PIN   D2
#define SCL_PIN   D1
#define I2C_HZ    400000UL

WiFiUDP udp;
Adafruit_MPU6050 mpu;

// ------------------- Helpers -------------------
static inline IPAddress broadcastIP() {
  IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();
  return IPAddress((uint32_t)ip | ~((uint32_t)mask));
}

bool connectWiFiBlocking(uint32_t timeout_ms = 15000) {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeout_ms) {
    delay(50);
    yield();
  }
  return WiFi.status() == WL_CONNECTED;
}

// ---- Your exact sonar read logic, wrapped in a function ----
bool readSonar(float& mm_out, float& cm_out, unsigned long& echo_us_out) {
  // Trigger a 10 µs pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Measure the echo HIGH time in microseconds
  unsigned long echo_us = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  echo_us_out = echo_us;

  if (echo_us == 0) {
    return false; // timeout / out-of-range
  } else {
    float mm = (echo_us * SOUND_SPEED_MM_PER_US) * 0.5f;
    mm_out = mm;
    cm_out = mm * 0.1f;
    return true;
  }
}

// ------------------- Arduino -------------------
void setup() {
  Serial.begin(115200);
  delay(150);

  // Sonar pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT); // ⚠ ensure 3.3V-safe ECHO
  digitalWrite(TRIG_PIN, LOW);

  // I2C + MPU6050 (init once)
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_HZ);
  if (!mpu.begin()) {
    Serial.println(F("MPU6050 not found; check wiring."));
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_94_HZ); // low latency
  }

  // Wi-Fi + UDP
  bool wifiOK = connectWiFiBlocking(15000);
  udp.begin(0); // ephemeral sender port

#if DEBUG
  Serial.println();
  Serial.println(F("HC-SR04 + MPU6050 → UDP (wrapper module)"));
  Serial.printf("Node: %s\n", NODE_ID);
  Serial.printf("Pins: TRIG=%d, ECHO=%d | I2C SDA=%d SCL=%d\n", TRIG_PIN, ECHO_PIN, SDA_PIN, SCL_PIN);
  Serial.printf("WiFi: %s (%s)\n", WIFI_SSID, wifiOK ? WiFi.localIP().toString().c_str() : "not connected");
#endif
}

void loop() {
  // Wi-Fi auto-reconnect with reboot fallback
  static uint8_t failCycles = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (connectWiFiBlocking(4000)) failCycles = 0;
    else if (++failCycles >= 10)   ESP.restart();
  }

  // --- Read sonar (your exact routine) ---
  float mm = NAN, cm = NAN;
  unsigned long echo_us = 0;
  bool sOK = readSonar(mm, cm, echo_us);

  // --- Read IMU (fast) ---
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);   // accel m/s^2, gyro deg/s, temp °C

  // --- Build JSON in a fixed buffer (no heap churn) ---
  char buf[320];
  int n = 0;
  if (sOK) {
    n = snprintf(buf, sizeof(buf),
      "{\"node\":\"%s\","
      "\"sonar\":{\"echo_us\":%lu,\"mm\":%.1f,\"cm\":%.2f},"
      "\"imu\":{\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
              "\"gx\":%.3f,\"gy\":%.3f,\"gz\":%.3f,\"t\":%.2f}}",
      NODE_ID, (unsigned long)millis(),
      echo_us, mm, cm,
      a.acceleration.x, a.acceleration.y, a.acceleration.z,
      g.gyro.x, g.gyro.y, g.gyro.z, temp.temperature
    );
  } else {
    n = snprintf(buf, sizeof(buf),
      "{\"node\":\"%s\","
      "\"sonar\":{\"timeout\":1},"
      "\"imu\":{\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
              "\"gx\":%.3f,\"gy\":%.3f,\"gz\":%.3f,\"t\":%.2f}}",
      NODE_ID, (unsigned long)millis(),
      a.acceleration.x, a.acceleration.y, a.acceleration.z,
      g.gyro.x, g.gyro.y, g.gyro.z, temp.temperature
    );
  }
  if (n < 0) n = 0; if (n > (int)sizeof(buf)) n = sizeof(buf);

  // --- UDP broadcast ---
  if (WiFi.status() == WL_CONNECTED) {
    udp.beginPacket(broadcastIP(), UDP_PORT);
    udp.write((const uint8_t*)buf, n);
    udp.endPacket();
  }

#if DEBUG
  // Console status (1 Hz)
  static uint32_t lastPrint = 0;
  uint32_t now = millis();
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    if (sOK) {
      Serial.printf("[SONAR] echo=%lu us  =>  %.1f mm  (%.2f cm)\n", echo_us, mm, cm);
    } else {
      Serial.println(F("[SONAR] timeout"));
    }
    Serial.print(F("[IMU] a(m/s^2): "));
    Serial.printf("%.3f %.3f %.3f", a.acceleration.x, a.acceleration.y, a.acceleration.z);
    Serial.print(F(" | g(deg/s): "));
    Serial.printf("%.3f %.3f %.3f", g.gyro.x, g.gyro.y, g.gyro.z);
    Serial.print(F(" | temp C: "));
    Serial.println(temp.temperature, 2);
  }
#endif

  // ~10 Hz like your original (adjust as you like)
  delay(10);
}