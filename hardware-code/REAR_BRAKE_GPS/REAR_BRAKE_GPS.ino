/*
  ESP8266 MLX90614 (hardware I2C only) + GPS(NEO-6M) + SONAR(HC-SR04) + WHEEL ISR → UDP
  - I2C: ONLY Bus A on Wire (SDA=D2, SCL=D1) for MLX90614
  - GPS: SoftwareSerial on D7 (RX) / D8 (TX), 9600 bps, TinyGPS++
  - SONAR: HC-SR04 with TRIG=D5, ECHO=D0  (⚠ level shift ECHO to 3.3V)
  - WHEEL SPEED: pulse input on D6 (GPIO12) via interrupt (RISING + dead-time)
  - Wi-Fi: GXXR / 1234567890gsxr (auto-reconnect)
  - UDP broadcast: port 5555
  - Serial: minimal boot prints; optional runtime debug (GPS_DEBUG)

  NOTE: Avoid D3 (GPIO0) for pulse input — it affects boot mode. D6 is safe.
*/

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <SoftwareSerial.h>
#include <TinyGPS++.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------- Debug toggle ----------
#define GPS_DEBUG 0

// ---------- Node / Wi-Fi / UDP ----------
#define NODE_ID     "REAR-BRAKE-GPS-SS"
const char* WIFI_SSID = "GXXR";
const char* WIFI_PASS = "1234567890gsxr";
const uint16_t UDP_PORT = 5555;
WiFiUDP udp;

// ---------- LED (D4 blue LED is active-LOW) ----------
#define LED_PIN   D4
inline void ledOn()  { digitalWrite(LED_PIN, LOW);  }
inline void ledOff() { digitalWrite(LED_PIN, HIGH); }

// ---------- I2C (hardware bus ONLY) ----------
#define SDAA_PIN  D2   // GPIO4
#define SCLA_PIN  D1   // GPIO5
#define I2C_HZ    100000UL
#define MAX_SENSORS 3

struct SensorEntry {
  uint8_t  addr;
  Adafruit_MLX90614 dev;
  bool ok;
};
SensorEntry sensors[MAX_SENSORS];
uint8_t sensorCount = 0;

// ---------- GPS (SoftSerial D7=RX, D8=TX) ----------
#define GPS_RX_PIN D7
#define GPS_TX_PIN D8
SoftwareSerial gpsSS(GPS_RX_PIN, GPS_TX_PIN);
TinyGPSPlus gps;

#if GPS_DEBUG
// Raw NMEA collection (dumped once per second)
#define NMEA_STORE    12
#define NMEA_MAXLEN   96
static char nmeaLines[NMEA_STORE][NMEA_MAXLEN];
static uint8_t nmeaCount = 0;
static char nmeaLineBuf[NMEA_MAXLEN];
static uint8_t nmeaIdx = 0;
inline void pushNmeaLine() {
  if (!nmeaIdx) return;
  if (nmeaCount < NMEA_STORE) {
    uint8_t len = (nmeaIdx >= (NMEA_MAXLEN-1)) ? (NMEA_MAXLEN-1) : nmeaIdx;
    memcpy(nmeaLines[nmeaCount], nmeaLineBuf, len);
    nmeaLines[nmeaCount][len] = 0; nmeaCount++;
  }
  nmeaIdx = 0;
}
inline void feedNmeaChar(char c) {
  if (c == '\r') return;
  if (c == '\n') { pushNmeaLine(); return; }
  if (nmeaIdx < (NMEA_MAXLEN-1)) nmeaLineBuf[nmeaIdx++] = c;
}
#endif

// ---------- SONAR (HC-SR04) ----------
#define SONAR_TRIG_PIN  D5
#define SONAR_ECHO_PIN  D0
const float SOUND_SPEED_MM_PER_US = 0.343f;    // ~20°C
const uint32_t ECHO_TIMEOUT_US = 30000UL;      // 30ms ≈ 5.1m round-trip
float sonarReadMM() {
  digitalWrite(SONAR_TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(SONAR_TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(SONAR_TRIG_PIN, LOW);
  unsigned long echo_us = pulseIn(SONAR_ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  if (!echo_us) return NAN;
  return (echo_us * SOUND_SPEED_MM_PER_US) * 0.5f;
}

// ---------- Utils ----------
static inline IPAddress broadcastIP() {
  IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();
  return IPAddress((uint32_t)ip | ~((uint32_t)mask));
}

bool connectWiFiBlocking(uint32_t timeout_ms = 20000) {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false); // better ISR latency
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const uint32_t blinkInterval = 400;
  uint32_t t0 = millis(), lastToggle = 0; bool led = false;

  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeout_ms) {
    uint32_t now = millis();
    if (now - lastToggle >= blinkInterval) {
      led = !led; if (led) ledOn(); else ledOff();
      lastToggle = now;
    }
    while (gpsSS.available()) {
      char c = gpsSS.read();
      gps.encode(c);
#if GPS_DEBUG
      feedNmeaChar(c);
#endif
    }
    delay(10);
    yield();
  }
  ledOff();
  return WiFi.status() == WL_CONNECTED;
}

// Scan ONLY the hardware I2C bus and init up to MAX_SENSORS MLX90614
void discoverSensorsHW() {
  sensorCount = 0;
  for (uint8_t a = 0x03; a < 0x78 && sensorCount < MAX_SENSORS; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      if (sensors[sensorCount].dev.begin(a)) {
        sensors[sensorCount].addr = a;
        sensors[sensorCount].ok   = true;
        sensorCount++;
      }
    }
    yield();
  }
}

// Wheel circumference in meters 
#define WHEEL_CIRC_M      0.3141592653589793f
#define PULSES_PER_REV    1
#define WHEEL_PIN D6
// Polling + debounce settings
#define DEBOUNCE_MS       3      // require stable state for this long
#define MIN_PULSE_GAP_MS  8      // ignore pulses closer than this (noise/bounce)
#define STOP_TIMEOUT_MS   500    // if no pulse for this long -> speed = 0

// For INPUT_PULLUP sensors, "active" is often LOW during the pulse
#define PULSE_ACTIVE      LOW

const float DIST_PER_PULSE_M = WHEEL_CIRC_M / PULSES_PER_REV;

uint8_t rawState, lastRawState;
uint8_t stableState;

uint32_t lastChangeMs = 0;
uint32_t lastPulseMs  = 0;

float speedKmh = 0.0f;

void handleWheelSpeed(){
  uint32_t now = millis();
  rawState = digitalRead(WHEEL_PIN);

  // Track raw changes (for debounce)
  if (rawState != lastRawState) {
    lastRawState = rawState;
    lastChangeMs = now;
  }

  // If raw input has been stable long enough, accept it as stable
  if ((now - lastChangeMs) >= DEBOUNCE_MS && stableState != rawState) {
    uint8_t prevStable = stableState;
    stableState = rawState;

    // Detect edge into the active pulse state (e.g., HIGH->LOW)
    if (stableState == PULSE_ACTIVE && prevStable != PULSE_ACTIVE) {

      // Ignore pulses too close together (extra protection)
      if (lastPulseMs != 0 && (now - lastPulseMs) >= MIN_PULSE_GAP_MS) {
        float dtSec = (now - lastPulseMs) / 1000.0f;
        float vMps = DIST_PER_PULSE_M / dtSec;
        float vKmh = vMps * 3.6f;

        // Light smoothing (optional)
        speedKmh = 0.3f * vKmh + 0.7f * speedKmh;
      }

      lastPulseMs = now;
    }
  }

  // Stop detection
  if (lastPulseMs != 0 && (now - lastPulseMs) > STOP_TIMEOUT_MS) {
    speedKmh = 0.0f;
  }

}

// ---------- Arduino ----------
void setup() {
  Serial.begin(115200);
  delay(150);

  pinMode(LED_PIN, OUTPUT);
  ledOff();

  // I2C
  pinMode(SDAA_PIN, INPUT_PULLUP);
  pinMode(SCLA_PIN, INPUT_PULLUP);
  Wire.begin(SDAA_PIN, SCLA_PIN);
  Wire.setClock(I2C_HZ);

  // MLX
  discoverSensorsHW();

  // GPS
  gpsSS.begin(9600);

  // SONAR pins
  pinMode(SONAR_TRIG_PIN, OUTPUT);
  pinMode(SONAR_ECHO_PIN, INPUT); // ⚠ use level shifter/divider if 5V HC-SR04
  digitalWrite(SONAR_TRIG_PIN, LOW);

  pinMode(WHEEL_PIN, INPUT_PULLUP);   // use INPUT if module actively drives HIGH/LOW

  // Wi-Fi & UDP
  bool ok = connectWiFiBlocking(20000);
  udp.begin(0);

  // Boot prints (minimal)
  Serial.println();
  Serial.println(F("ESP8266 MLX90614 + GPS + SONAR + WHEEL → UDP"));
  Serial.printf("Node: %s\n", NODE_ID);
  Serial.printf("WiFi: %s (%s)\n",
    WIFI_SSID, ok ? WiFi.localIP().toString().c_str() : "not connected");
  Serial.printf("MLX sensors: %u (S1..S%u)\n", sensorCount, sensorCount);
 
#if GPS_DEBUG
  Serial.println(F("DEBUG ON"));
#else
  Serial.println(F("DEBUG OFF"));
#endif
  Serial.flush();

  rawState = lastRawState = stableState = digitalRead(D6);
  lastChangeMs = millis();
}

void loop() {

   handleWheelSpeed();

  // Feed GPS (and collect raw if enabled)
  for (uint8_t i = 0; i < 64 && gpsSS.available(); i++) {
    char c = gpsSS.read();
    gps.encode(c);
#if GPS_DEBUG
    feedNmeaChar(c);
#endif
  }

  // Wi-Fi autoreconnect with reboot fallback
  static uint8_t failCycles = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (connectWiFiBlocking(10000)) { failCycles = 0; }
    else if (++failCycles >= 3) { ESP.restart(); }
  }

  // Update wheel speed estimate on schedule
  
  // ----- Build JSON -----
  String js; js.reserve(320);
  js += "{\"node\":\""; js += NODE_ID; js += "\"";

  // MLX S1..S3
  for (uint8_t i = 0; i < sensorCount; i++) {
    double objC = sensors[i].dev.readObjectTempC();
    if (!isnan(objC) && objC > -70 && objC < 400) {
      js += ",\"S"; js += String(i + 1); js += "\":";
      js += String(objC, 2);
    }
    yield();
  }

  // SONAR
  float dmm = sonarReadMM();
  if (!isnan(dmm)) {
    js += ",\"rear_ss\":{\"mm\":"; js += String(dmm, 1); js += "}";
  }

  // GPS block (validity-gated)
  bool haveLoc = gps.location.isValid();
  bool haveAlt = gps.altitude.isValid();
  bool haveSpd = gps.speed.isValid();
  bool haveSat = gps.satellites.isValid();

  if (haveLoc || haveAlt || haveSpd || haveSat) {
    js += ",\"gps\":{";
    bool first = true;
    if (haveLoc) { js += "\"lat\":"; js += String(gps.location.lat(), 6);
                   js += ",\"lon\":"; js += String(gps.location.lng(), 6); first = false; }
    if (haveAlt) { if (!first) js += ","; js += "\"alt\":"; js += String(gps.altitude.meters(), 1); first = false; }
    if (haveSpd) { if (!first) js += ","; js += "\"spd\":"; js += String(gps.speed.kmph(), 2);     first = false; }
    if (haveSat) { if (!first) js += ","; js += "\"sats\":"; js += String((unsigned)gps.satellites.value()); first = false; }
    if (!first) js += ",";
    js += "\"fix\":"; js += (gps.location.isValid() ? "1" : "0");
    js += "}";
  }

  // WHEEL
  js += ",\"wheel\":{";
  
  js += "\"kmh\":"; js += String(speedKmh, 2);

  js += "}";

  js += "}";

  // ----- UDP broadcast -----
  if (WiFi.status() == WL_CONNECTED) {
    udp.beginPacket(broadcastIP(), UDP_PORT);
    udp.write((const uint8_t*)js.c_str(), js.length());
    udp.endPacket();
  }

#if GPS_DEBUG
  // Optional 1 Hz printout
  static uint32_t lastPrint = 0;
  uint32_t now = millis();
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    Serial.print(F("[WHEEL] cnt=")); Serial.print(wheelCountSnapshot);
    Serial.print(F(" kmh=")); Serial.println(wheelKmh, 2);

    Serial.print(F("[SONAR] "));
    if (!isnan(dmm)) { Serial.print(dmm, 1); Serial.println(F(" mm")); }
    else             { Serial.println(F("timeout")); }

    Serial.print(F("[GPS] fix="));  Serial.print(gps.location.isValid() ? "1" : "0");
    Serial.print(F(" sats="));      Serial.print(gps.satellites.isValid() ? (int)gps.satellites.value() : -1);
    Serial.print(F(" lat="));       if (gps.location.isValid()) Serial.print(gps.location.lat(), 6); else Serial.print(F("nan"));
    Serial.print(F(" lon="));       if (gps.location.isValid()) Serial.print(gps.location.lng(), 6); else Serial.print(F("nan"));
    Serial.print(F(" spd_kmph="));  if (gps.speed.isValid())    Serial.print(gps.speed.kmph(), 2);   else Serial.print(F("nan"));
    Serial.print(F(" alt_m="));     if (gps.altitude.isValid()) Serial.print(gps.altitude.meters(),1); else Serial.print(F("nan"));
    Serial.println();

    if (nmeaIdx) pushNmeaLine();
    if (nmeaCount) {
      Serial.println(F("[NMEA dump]"));
      for (uint8_t i = 0; i < nmeaCount; i++) { Serial.print(F("[NMEA] ")); Serial.println(nmeaLines[i]); }
      nmeaCount = 0;
    }
  }
#endif
}