/*
  ESP8266 MLX90614 (HW I2C) + D6 input → UDP broadcast (port 8888)
  - I2C: MLX90614 on Wire (SDA=D2, SCL=D1)
  - D6:  digital input with pull-up (reads 1 when open, 0 when grounded)
  - Wi-Fi: SSID "GXXR", PASS "1234567890gsxr" (auto-reconnect)
  - JSON: {"node":"TEMP-D6","ts":123,"tempC":..,"d6":0|1}
  - Debug: set DEBUG=1 to print once/sec
*/

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>

// ------------ Config ------------
#define DEBUG        0
#define NODE_ID      "PORT4-D6-INT"

const char* WIFI_SSID = "GXXR";
const char* WIFI_PASS = "1234567890gsxr";
const uint16_t UDP_PORT = 8888;

#define LED_PIN    D4            // onboard blue LED (active-LOW)
#define SDA_PIN    D2
#define SCL_PIN    D1
#define I2C_HZ     100000UL

#define INPUT_PIN  D6            // input with pull-up

// ------------ Globals ------------
WiFiUDP udp;
Adafruit_MLX90614 mlx;
bool mlx_ok = false;
uint8_t mlx_addr = 0x5A;         // default MLX90614 address

// ------------ Helpers ------------
inline void ledOn()  { digitalWrite(LED_PIN, LOW);  }
inline void ledOff() { digitalWrite(LED_PIN, HIGH); }

static inline IPAddress broadcastIP() {
  IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();
  return IPAddress((uint32_t)ip | ~((uint32_t)mask));
}

// Try default address; if that fails, scan to find the first MLX that responds
bool initMLX() {
  if (mlx.begin(mlx_addr)) return true;
  for (uint8_t a = 0x03; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      if (mlx.begin(a)) { mlx_addr = a; return true; }
    }
    yield();
  }
  return false;
}

// Slow LED flicker while connecting; return true on success
bool connectWiFiBlocking(uint32_t timeout_ms = 20000) {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const uint32_t blink = 400;
  uint32_t t0 = millis(), last = 0; bool on = false;

  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeout_ms) {
    uint32_t now = millis();
    if (now - last >= blink) { on = !on; if (on) ledOn(); else ledOff(); last = now; }
    delay(10);
    yield();
  }
  ledOff();
  return (WiFi.status() == WL_CONNECTED);
}

// ------------ Arduino ------------
void setup() {
  Serial.begin(115200);
  delay(150);

  pinMode(LED_PIN, OUTPUT);
  ledOff();

  // I2C
  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, INPUT_PULLUP);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_HZ);

  // MLX
  mlx_ok = initMLX();

  // Input pin
  pinMode(INPUT_PIN, INPUT_PULLUP);

  // Wi-Fi & UDP
  bool wifiOK = connectWiFiBlocking(20000);
  udp.begin(0);  // ephemeral sender port

  // Boot prints (minimal)
  Serial.println();
  Serial.println(F("ESP8266 MLX90614 + D6 input → UDP"));
  Serial.printf("Node: %s\n", NODE_ID);
  Serial.printf("WiFi: %s (%s)\n", WIFI_SSID, wifiOK ? WiFi.localIP().toString().c_str() : "not connected");
  Serial.printf("MLX90614: %s (addr 0x%02X)\n", mlx_ok ? "OK" : "NOT FOUND", mlx_addr);
#if DEBUG
  Serial.println(F("Debug: ON (1 Hz)"));
#else
  Serial.println(F("Debug: OFF"));
#endif
  Serial.flush();
}

void loop() {
  // Wi-Fi autoreconnect with reboot fallback
  static uint8_t failCycles = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (connectWiFiBlocking(8000)) failCycles = 0;
    else if (++failCycles >= 5) ESP.restart();
  }

  // Read inputs
  int d6 = digitalRead(INPUT_PIN);   // 1 = pull-up (open), 0 = grounded

  // Read temperature (object temp in °C)
  float tC = NAN;
  if (mlx_ok) {
    float v = mlx.readObjectTempC();
    if (!isnan(v) && v > -70 && v < 400) tC = v;
  }

  // Build compact JSON
  char buf[192];
  int neutralTrigger = analogRead(A0);

  int n;
  if (!isnan(tC)) {
    n = snprintf(buf, sizeof(buf),
      "{\"node\":\"%s\",\"tempC\":%.2f,\"d6\":%d, \"neutral\": %d}",
      NODE_ID, tC, d6, neutralTrigger);
  } else {
    tC = 0;
    // omit tempC if sensor missing/invalid
    n = snprintf(buf, sizeof(buf),
      "{\"node\":\"%s\",\"tempC\":%.2f,\"d6\":%d, \"neutral\": %d}",
      NODE_ID, tC , d6, neutralTrigger);
  }
  if (n < 0) n = 0; if (n > (int)sizeof(buf)) n = sizeof(buf);

  // UDP broadcast
  if (WiFi.status() == WL_CONNECTED) {
    udp.beginPacket(broadcastIP(), UDP_PORT);
    udp.write((const uint8_t*)buf, n);
    udp.endPacket();
  }

#if DEBUG
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last >= 1000) {
    last = now;
    Serial.print(F("[D6]=")); Serial.print(d6);
    Serial.print(F("  [tempC]=")); if (!isnan(tC)) Serial.println(tC, 2); else Serial.println(F("nan"));
  }
#endif

  // no fixed delay; keep loop responsive
  delay(1);
}