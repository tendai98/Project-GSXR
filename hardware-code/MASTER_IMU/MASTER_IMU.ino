/*
  ESP8266 Dual-Bus MPU6050 → UDP (send+listen, WiFi LED, autoreconnect)
  - Two I2C buses by time-multiplexing Wire across two pin pairs (A & B)
  - Adafruit_MPU6050 on each bus
  - Wi-Fi: GXXR / 1234567890gsxr
  - UDP broadcast + listen on port 1111
  - Minimal Serial at boot only
  - LED D4 (active-LOW): slow flicker during connect; 50ms blip on TX; 20ms tick on RX
*/

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ---------- Node / Wi-Fi / UDP ----------
#define NODE_ID     "MASTER-IMU"
const char* WIFI_SSID = "GXXR";
const char* WIFI_PASS = "1234567890gsxr";
const uint16_t UDP_PORT = 2222;
WiFiUDP udp;

// ---------- LED (D4 blue LED is active-LOW) ----------
#define LED_PIN D4
inline void ledOn()  { digitalWrite(LED_PIN, LOW);  }
inline void ledOff() { digitalWrite(LED_PIN, HIGH); }

// ---------- I2C pins (two physical buses) ----------
#define SDAA_PIN  D2  // Bus A SDA (GPIO4)
#define SCLA_PIN  D1  // Bus A SCL (GPIO5)
#define SDAB_PIN  D6  // Bus B SDA (GPIO12)
#define SCLB_PIN  D5  // Bus B SCL (GPIO14)
#define I2C_HZ    100000UL   // try 200000/400000 if wiring is short/clean

// ---------- Forward declarations to beat Arduino's auto-prototype ----------
struct MPURef;
void   selectBus(char bus);
bool   i2cAddrPresent(uint8_t addr);
bool   initMPU(MPURef& r);
bool   connectWiFiBlocking(uint32_t timeout_ms = 20000);
void   handleUdpRx();

// ---------- Two sensors (one per bus) ----------
struct MPURef {
  char bus;            // 'A' or 'B'
  uint8_t addr;        // 0x68 or 0x69
  Adafruit_MPU6050 dev;
  bool ok;
};

MPURef mpuA = { 'A', 0x68, Adafruit_MPU6050(), false };
MPURef mpuB = { 'B', 0x68, Adafruit_MPU6050(), false };

// ---------- Helpers ----------
static inline IPAddress broadcastIP() {
  IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();
  return IPAddress((uint32_t)ip | ~((uint32_t)mask));
}

void selectBus(char bus) {
  if (bus == 'A') Wire.begin(SDAA_PIN, SCLA_PIN);
  else            Wire.begin(SDAB_PIN, SCLB_PIN);
  Wire.setClock(I2C_HZ);
  delay(1);
}

bool i2cAddrPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

// Initialize one MPU on its bus; prefer r.addr, fallback to the alternate
bool initMPU(MPURef& r) {
  selectBus(r.bus);
  uint8_t tryAddrs[2] = { r.addr, (uint8_t)(r.addr == 0x68 ? 0x69 : 0x68) };
  for (uint8_t i = 0; i < 2; i++) {
    uint8_t a = tryAddrs[i];
    if (!i2cAddrPresent(a)) continue;
    if (r.dev.begin(a, &Wire)) {
      r.addr = a;
      r.dev.setAccelerometerRange(MPU6050_RANGE_8_G);
      r.dev.setGyroRange(MPU6050_RANGE_500_DEG);
      r.dev.setFilterBandwidth(MPU6050_BAND_44_HZ);
      r.ok = true;
      return true;
    }
  }
  r.ok = false;
  return false;
}

// Slow LED flicker while connecting
bool connectWiFiBlocking(uint32_t timeout_ms) {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const uint32_t blinkInterval = 400;
  uint32_t t0 = millis(), last = 0;
  bool on = false;

  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeout_ms) {
    uint32_t now = millis();
    if (now - last >= blinkInterval) {
      on = !on; if (on) ledOn(); else ledOff();
      last = now;
    }
    delay(10);
    yield();
  }
  ledOff();
  return WiFi.status() == WL_CONNECTED;
}

// Consume inbound UDP silently; tiny LED tick to indicate RX
void handleUdpRx() {
  static char buf[128];
  int len;
  while ((len = udp.parsePacket()) > 0) {
    int n = udp.read(buf, sizeof(buf) - 1);
    if (n < 0) n = 0; buf[n] = 0;
    ledOn(); delay(20); ledOff();
  }
}

// ---------- Arduino ----------
void setup() {
  Serial.begin(115200);
  delay(150);
  pinMode(LED_PIN, OUTPUT);
  ledOff();

  // (Recommend external 4.7k–10k pull-ups on each bus)
  pinMode(SDAA_PIN, INPUT_PULLUP); pinMode(SCLA_PIN, INPUT_PULLUP);
  pinMode(SDAB_PIN, INPUT_PULLUP); pinMode(SCLB_PIN, INPUT_PULLUP);

  // Init sensors: one per bus (same address OK since buses are separate)
  mpuA.addr = 0x68; mpuB.addr = 0x68;
  bool aOK = initMPU(mpuA);
  bool bOK = initMPU(mpuB);

  bool wifiOK = connectWiFiBlocking(20000);
  udp.begin(UDP_PORT);   // listen + send on 1111

  // ---- Minimal boot prints ----
  Serial.println();
  Serial.println(F("ESP8266 Dual-Bus MPU6050 → UDP (send+listen)"));
  Serial.printf("Node: %s\n", NODE_ID);
  Serial.printf("WiFi: %s (%s)\n",
    WIFI_SSID, wifiOK ? WiFi.localIP().toString().c_str() : "not connected");
  Serial.printf("MPU A: bus %c, %s @ 0x%02X\n", mpuA.bus, aOK ? "OK" : "X", mpuA.addr);
  Serial.printf("MPU B: bus %c, %s @ 0x%02X\n", mpuB.bus, bOK ? "OK" : "X", mpuB.addr);
  Serial.flush();
}

void loop() {
  // --- Reconnect with reboot fallback ---
  static uint8_t failCycles = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (connectWiFiBlocking(10000)) {
      failCycles = 0;
      udp.begin(UDP_PORT); // rebind after reconnect
    } else if (++failCycles >= 3) {
      ESP.restart();
    }
  }

  // --- Read sensors ---
  sensors_event_t a1, g1, t1, a2, g2, t2;
  bool gotA = false, gotB = false;

  if (mpuA.ok) {
    selectBus(mpuA.bus);
    mpuA.dev.getEvent(&a1, &g1, &t1); // Adafruit fills by reference
    gotA = true;
  }
  if (mpuB.ok) {
    selectBus(mpuB.bus);
    mpuB.dev.getEvent(&a2, &g2, &t2);
    gotB = true;
  }

  // Convert to g and deg/s (Adafruit gives m/s^2 and rad/s)
  const float G_INV = 1.0f / 9.80665f;
  const float RAD2DEG = 57.2957795f;

  // --- Build JSON: top-level node + separate mpuA/mpuB objects ---
  String js; js.reserve(256);
  js += "{\"node\":\"" NODE_ID "\",\"ts\":";
  js += String((uint32_t)millis());

  if (gotA) {
    js += ",\"mpuA\":{\"a\":{\"x\":";
    js += String(a1.acceleration.x * G_INV, 3);
    js += ",\"y\":";
    js += String(a1.acceleration.y * G_INV, 3);
    js += ",\"z\":";
    js += String(a1.acceleration.z * G_INV, 3);
    js += "},\"g\":{\"x\":";
    js += String(g1.gyro.x * RAD2DEG, 2);
    js += ",\"y\":";
    js += String(g1.gyro.y * RAD2DEG, 2);
    js += ",\"z\":";
    js += String(g1.gyro.z * RAD2DEG, 2);
    js += "},\"t\":";
    js += String(t1.temperature, 2);
    js += "}";
  }

  if (gotB) {
    js += ",\"mpuB\":{\"a\":{\"x\":";
    js += String(a2.acceleration.x * G_INV, 3);
    js += ",\"y\":";
    js += String(a2.acceleration.y * G_INV, 3);
    js += ",\"z\":";
    js += String(a2.acceleration.z * G_INV, 3);
    js += "},\"g\":{\"x\":";
    js += String(g2.gyro.x * RAD2DEG, 2);
    js += ",\"y\":";
    js += String(g2.gyro.y * RAD2DEG, 2);
    js += ",\"z\":";
    js += String(g2.gyro.z * RAD2DEG, 2);
    js += "},\"t\":";
    js += String(t2.temperature, 2);
    js += "}";
  }

  js += "}";

 

  // --- Broadcast and listen ---
  if (WiFi.status() == WL_CONNECTED) {
    udp.beginPacket(broadcastIP(), UDP_PORT);
    udp.write((const uint8_t*)js.c_str(), js.length());
    udp.endPacket();
  }
  handleUdpRx();                    // RX tick inside

  // No long delays to keep it fast
}
