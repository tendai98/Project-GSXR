/*
  ESP8266 D5/D6 digital inputs (INPUT_PULLUP) + MPU6050 → UDP broadcast (port 6666)
  - Inputs: D5, D6 (0 = pulled LOW/active; 1 = HIGH via pull-up)
  - IMU:    MPU6050 on I2C (SDA=D2, SCL=D1)
  - Wi-Fi:  SSID "GXXR", PASS "1234567890gsxr" (auto-reconnect)
  - JSON:   {"node":"IO-IMU","ts":..., "in":{"d5":0/1,"d6":0/1},
             "imu":{"ax":..,"ay":..,"az":..,"gx":..,"gy":..,"gz":..,"t":..}}
  - Debug:  set DEBUG=1 to print once per second
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ------------------- Config -------------------
#define DEBUG 1

#define NODE_ID     "BRAKES-IMU"
const char* WIFI_SSID = "GXXR";
const char* WIFI_PASS = "1234567890gsxr";
const uint16_t UDP_PORT = 7777;

// Digital inputs
#define IN1_PIN  D5   // GPIO14
#define IN2_PIN  D6   // GPIO12

// I2C / MPU6050
#define SDA_PIN  D2
#define SCL_PIN  D1
#define I2C_HZ   400000UL   // drop to 100k if your wiring is long/noisy

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

// ------------------- Arduino -------------------
void setup() {
  Serial.begin(115200);
  delay(150);

  // Inputs (internal pull-ups on)
  pinMode(IN1_PIN, INPUT_PULLUP);
  pinMode(IN2_PIN, INPUT_PULLUP);

  // I2C + IMU
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_HZ);
  if (!mpu.begin()) {
    Serial.println(F("MPU6050 not found; check wiring."));
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_94_HZ);
  }

  // Wi-Fi + UDP
  bool wifiOK = connectWiFiBlocking(15000);
  udp.begin(0); // ephemeral sender port

#if DEBUG
  Serial.println();
  Serial.println(F("ESP8266 IO(D5/D6) + MPU6050 → UDP"));
  Serial.printf("Node: %s\n", NODE_ID);
  Serial.printf("WiFi: %s (%s)\n",
    WIFI_SSID, wifiOK ? WiFi.localIP().toString().c_str() : "not connected");
#endif
}

void loop() {
  // Wi-Fi auto-reconnect with reboot fallback
  static uint8_t failCycles = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (connectWiFiBlocking(4000)) {
      failCycles = 0;
    } else if (++failCycles >= 10) {
      ESP.restart();
    }
  }

  // Read inputs (INPUT_PULLUP → 1 = idle/high, 0 = grounded/active)
  int d5 = digitalRead(IN1_PIN) ? 1 : 0;
  int d6 = digitalRead(IN2_PIN) ? 1 : 0;

  // Read IMU
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);   // accel m/s^2, gyro deg/s, temp °C

  // Build JSON (fixed buffer; fast and GC-free)
  char buf[320];
  int n = snprintf(buf, sizeof(buf),
    "{\"node\":\"%s\",\"ts\":%lu,"
    "\"in\":{\"d5\":%d,\"d6\":%d},"
    "\"imu\":{\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
            "\"gx\":%.3f,\"gy\":%.3f,\"gz\":%.3f,\"t\":%.2f}}",
    NODE_ID, (unsigned long)millis(),
    d5, d6,
    a.acceleration.x, a.acceleration.y, a.acceleration.z,
    g.gyro.x, g.gyro.y, g.gyro.z,
    temp.temperature
  );
  if (n < 0) n = 0; if (n > (int)sizeof(buf)) n = sizeof(buf);

  // UDP broadcast
  if (WiFi.status() == WL_CONNECTED) {
    udp.beginPacket(broadcastIP(), UDP_PORT);
    udp.write((const uint8_t*)buf, n);
    udp.endPacket();
  }

#if DEBUG
  static uint32_t lastPrint = 0;
  uint32_t now = millis();
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    Serial.printf("[IN] D5=%d D6=%d\n", d5, d6);
    Serial.print(F("[IMU] a(m/s^2): "));
    Serial.printf("%.3f %.3f %.3f", a.acceleration.x, a.acceleration.y, a.acceleration.z);
    Serial.print(F(" | g(deg/s): "));
    Serial.printf("%.3f %.3f %.3f", g.gyro.x, g.gyro.y, g.gyro.z);
    Serial.print(F(" | temp C: "));
    Serial.println(temp.temperature, 2);
  }
#endif

  // Light pacing to keep Wi-Fi stack happy
  delay(1);
}