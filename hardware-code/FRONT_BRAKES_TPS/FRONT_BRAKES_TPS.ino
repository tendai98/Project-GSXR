/*
  ESP8266 Dual-Bus MLX90614 → UDP (WiFi status LED + autoreconnect)

  - Two physical I2C buses on different pins (A and B), read sequentially
    by reinitializing Wire to each pin pair (time-multiplexed).
  - Uses Adafruit_MLX90614 to read Object temperature (°C).
  - Wi-Fi: SSID "GXXR", PASS "1234567890gsxr"
  - UDP broadcast → port 4444
  - Prints only at boot.
  - LED (D4, active-LOW): slow flicker during WiFi connect; short blip after each send.

  Board: ESP8266 (NodeMCU / Wemos D1 mini)
  Serial: 115200
*/

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>

// ---------- Node / Wi-Fi / UDP ----------
#define NODE_ID     "FRONT-T"
const char* WIFI_SSID = "GXXR";
const char* WIFI_PASS = "1234567890gsxr";
const uint16_t UDP_PORT = 4444;
WiFiUDP udp;

// ---------- LED (NodeMCU blue LED is active-LOW on D4/GPIO2) ----------
#define LED_PIN   D4
inline void ledOn()  { digitalWrite(LED_PIN, LOW);  } // active-LOW
inline void ledOff() { digitalWrite(LED_PIN, HIGH); }

// ---------- I2C pin sets ----------
#define SDAA_PIN  D2   // Bus A SDA (GPIO4)
#define SCLA_PIN  D1   // Bus A SCL (GPIO5)
#define SDAB_PIN  D6   // Bus B SDA (GPIO12)
#define SCLB_PIN  D5   // Bus B SCL (GPIO14)

// Try 100k first; you can raise to 200k or 400k if wiring is short/stable
#define I2C_HZ    100000UL

#define MAX_SENSORS 3   // publish S1..S3 only

struct SensorEntry {
  char     bus;              // 'A' or 'B'
  uint8_t  addr;             // I2C address
  Adafruit_MLX90614 dev;     // uses global Wire; we switch pins before reads
};

SensorEntry sensors[MAX_SENSORS];
uint8_t sensorCount = 0;

// ---------- Utils ----------
static inline IPAddress broadcastIP() {
  IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();
  return IPAddress((uint32_t)ip | ~((uint32_t)mask));
}

void selectBus(char bus) {
  if (bus == 'A') {
    Wire.begin(SDAA_PIN, SCLA_PIN);
  } else {
    Wire.begin(SDAB_PIN, SCLB_PIN);
  }
  Wire.setClock(I2C_HZ);
  delay(1); // tiny settle
}

uint8_t scanBus(char bus, uint8_t *out, uint8_t maxOut) {
  selectBus(bus);
  uint8_t n = 0;
  for (uint8_t a = 3; a < 0x78 && n < maxOut; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      out[n++] = a;
    }
    yield();
  }
  return n;
}

void addSensorsFromBus(char bus) {
  uint8_t found[8];
  uint8_t n = scanBus(bus, found, sizeof(found));
  for (uint8_t i = 0; i < n && sensorCount < MAX_SENSORS; i++) {
    uint8_t addr = found[i];
    selectBus(bus);
    if (!sensors[sensorCount].dev.begin(addr)) continue; // skip non-MLX
    sensors[sensorCount].bus  = bus;
    sensors[sensorCount].addr = addr;
    sensorCount++;
  }
}

// Slow flicker while connecting; return true on success
bool connectWiFiBlocking(uint32_t timeout_ms = 20000) {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const uint32_t blinkInterval = 400; // slow flicker (ms)
  uint32_t t0 = millis(), lastToggle = 0;
  bool led = false;

  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeout_ms) {
    uint32_t now = millis();
    if (now - lastToggle >= blinkInterval) {
      led = !led;
      if (led) ledOn(); else ledOff();
      lastToggle = now;
    }
    delay(10);
    yield();
  }
  ledOff(); // leave LED off after attempt
  return WiFi.status() == WL_CONNECTED;
}

// ---------- Arduino ----------
void setup() {
  Serial.begin(115200);
  delay(150);

  pinMode(LED_PIN, OUTPUT);
  ledOff(); // OFF (active-LOW)

  // Weak internal pull-ups (still use external 4.7k–10k on each bus)
  pinMode(SDAA_PIN, INPUT_PULLUP);
  pinMode(SCLA_PIN, INPUT_PULLUP);
  pinMode(SDAB_PIN, INPUT_PULLUP);
  pinMode(SCLB_PIN, INPUT_PULLUP);

  // Start on Bus A so Adafruit begin() has a valid Wire initially
  selectBus('A');

  // Discover sensors: fill S1..S3 in order A then B
  addSensorsFromBus('A');
  addSensorsFromBus('B');

  // Wi-Fi & UDP
  bool ok = connectWiFiBlocking(20000);
  udp.begin(0); // sender port (ephemeral)

  // ----- Minimal boot prints only -----
  Serial.println();
  Serial.println(F("ESP8266 MLX90614 Dual-Bus → UDP"));
  Serial.printf("Node: %s\n", NODE_ID);
  Serial.printf("WiFi: %s (%s)\n", WIFI_SSID, ok ? WiFi.localIP().toString().c_str() : "not connected");
  Serial.printf("Sensors tracked: %u (S1..S%u)\n", sensorCount, sensorCount);
  for (uint8_t i = 0; i < sensorCount; i++) {
    Serial.printf("  S%u -> Bus %c @ 0x%02X\n", i+1, sensors[i].bus, sensors[i].addr);
  }
  Serial.flush();
}

void loop() {
  // --------- Reconnect logic ---------
  static uint8_t failCycles = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (connectWiFiBlocking(10000)) {
      failCycles = 0; // recovered
    } else {
      if (++failCycles >= 3) { // ~30s of failed attempts
        ESP.restart();         // hard reboot to recover radios/stack
      }
    }
  }

  // --------- Build compact JSON payload ---------
  String js; js.reserve(128);
  js += "{\"node\":\"BRAKES-TPS\"";

  // TPS from A0 (raw 0..1023; NodeMCU/D1 mini have onboard divider ~3.2V max)
  int tps = analogRead(A0);
  js += ",\"tps\":"; js += String(tps);

  // MLX object temps as S1..S3
  for (uint8_t i = 0; i < sensorCount; i++) {
    selectBus(sensors[i].bus);
    double objC = sensors[i].dev.readObjectTempC();  // object temp (°C)
    if (!isnan(objC) && objC > -70 && objC < 400) {
      js += ",\"S"; js += String(i + 1); js += "\":";
      js += String(objC, 2);
    }
    yield();
  }
  js += "}";

  // --- UDP broadcast ---
  udp.beginPacket(broadcastIP(), UDP_PORT);
  udp.write((const uint8_t*)js.c_str(), js.length());
  udp.endPacket();
}