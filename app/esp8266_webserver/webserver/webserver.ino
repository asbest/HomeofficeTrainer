#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>

// Minimal INA226 helper (2 mΩ shunt)
// Datasheet reference values: Current_LSB = MaxExpectedCurrent/32768
// With a very low shunt (0.002 Ω) choose Current_LSB so that calibration fits in 16-bit register.
// We'll target Current_LSB = 0.0005 A (0.5 mA per bit) -> Calibration = 0.00512 / (Current_LSB * Rshunt)
// Calibration = 0.00512 / (0.0005 * 0.002) = 0.00512 / 0.000001 = 5120 (0x1400)
// Power LSB per datasheet = 25 * Current_LSB = 25 * 0.0005 = 0.0125 W
// Voltage (bus) LSB = 1.25 mV

namespace INA226 {
  const uint8_t ADDR = 0x40; // adjust if A0/A1 strapped differently
  const uint8_t REG_CONFIG = 0x00;
  const uint8_t REG_SHUNT  = 0x01;
  const uint8_t REG_BUS    = 0x02;
  const uint8_t REG_POWER  = 0x03;
  const uint8_t REG_CURRENT= 0x04;
  const uint8_t REG_CALIB  = 0x05;
  const uint16_t CALIB_VALUE = 5120; // for 2 mΩ, Current_LSB=0.5mA

  bool writeRegister(uint8_t reg, uint16_t value) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    Wire.write((value >> 8) & 0xFF);
    Wire.write(value & 0xFF);
    return Wire.endTransmission() == 0;
  }
  bool readRegister(uint8_t reg, uint16_t &out) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)ADDR, 2) != 2) return false;
    out = (Wire.read() << 8) | Wire.read();
    return true;
  }
  bool begin() {
    // Reset + average 16 samples, VBUSCT=1.1ms, VSHCT=1.1ms, MODE=Shunt+Bus Continuous
    // Config bits: 0x4xxx reset, but we'll write full config directly.
    // Use: AVG=4 (16 samples), VBUSCT=6 (1.1ms), VSHCT=6 (1.1ms), MODE=111 (cont shunt+bus)
    uint16_t config = (4 << 9) | (6 << 6) | (6 << 3) | 0x7; // 0b AAA BBB CCC MMM
    if (!writeRegister(REG_CONFIG, config)) return false;
    delay(2);
    if (!writeRegister(REG_CALIB, CALIB_VALUE)) return false;
    delay(2);
    return true;
  }
  static uint16_t lastVraw = 0; // expose for debug
  bool readMeasurements(float &busVoltage, float &current, float &power) {
    uint16_t vraw, craw, praw;
    if (!readRegister(REG_BUS, vraw)) return false;
    if (!readRegister(REG_CURRENT, craw)) return false;
    if (!readRegister(REG_POWER, praw)) return false;
    // Convert
    // INA226: Bus Voltage LSB = 1.25 mV directly (no >>3 needed, unlike INA219 example code)
    busVoltage = vraw * 0.00125f;
    current = (int16_t)craw * 0.0005; // 0.5 mA per bit
    power = praw * 0.0125; // 12.5 mW per bit
    lastVraw = vraw;
    return true;
  }
}

// Optional: reduce max clients if memory constrained
#ifndef WEBSOCKETS_MAX_CLIENTS
#define WEBSOCKETS_MAX_CLIENTS 2
#endif

// WLAN
const char* ssid     = "Fritzi";
const char* password = "65770551205628807473";

AsyncWebServer server(80);            // HTTP server (could be unused, but required host for WS)
AsyncWebSocket ws("/ws");            // WebSocket endpoint at /ws

// Legacy analog smoothing removed (INA226 provides filtered readings)
const int analogPin = A0; // leftover (not used with INA226 now)

// State for INA226
bool inaReady = false;
unsigned long lastRssiLog = 0;
unsigned long lastWsBroadcast = 0; // heartbeat broadcast timer
const unsigned long WS_BROADCAST_INTERVAL_MS = 500; // 0.5s push (doubled sampling rate)
// Diagnostics counters
volatile unsigned long statConnects = 0;
volatile unsigned long statDisconnects = 0;
volatile unsigned long statErrors = 0;
volatile unsigned long statBroadcasts = 0;
volatile unsigned long statInaFail = 0;
volatile unsigned long statPongs = 0;
unsigned long lastStatPrint = 0;
unsigned long statWindowStart = 0;
unsigned long statBroadcastsWindow = 0;
float lastPower=0, lastVoltage=0, lastCurrent=0;
unsigned long slowInaCount = 0;
const unsigned long INA_SLOW_THRESHOLD_US = 3000; // >3ms considered slow read
// LED control: onboard LED (active LOW on most ESP8266 boards)
void updateLed() {
  if (ws.count() > 0) {
    digitalWrite(LED_BUILTIN, LOW); // ON
  } else {
    digitalWrite(LED_BUILTIN, HIGH); // OFF
  }
}

String buildJson(float power, float voltage, float current) {
  String json = "{";
  json += "\"power\":" + String(power, 3) + ',';
  json += "\"voltage\":" + String(voltage, 3) + ',';
  json += "\"current\":" + String(current, 3);
  json += '}';
  return json;
}

void broadcastSample() {
  float v=0, c=0, p=0;
  unsigned long t0 = micros();
  if (inaReady) {
    if (INA226::readMeasurements(v, c, p)) {
      // success
    } else {
      statInaFail++;
      Serial.println("INA226 read failed");
    }
  }
  #ifdef INA226_DEBUG_VOLT
    extern uint16_t INA226::lastVraw; // using the static inside namespace
    Serial.printf("[VDBG] raw=0x%04X -> %.3fV\n", INA226::lastVraw, v);
  #endif
  unsigned long dur = micros() - t0;
  if (dur > INA_SLOW_THRESHOLD_US) {
    slowInaCount++;
    Serial.printf("[WARN] Slow INA226 read %lu us\n", dur);
  }
  lastPower = p; lastVoltage = v; lastCurrent = c;
  statBroadcasts++;
  statBroadcastsWindow++;
  String json = buildJson(p, v, c);
  ws.textAll(json);
}

void onWsEvent(AsyncWebSocket       * server,
               AsyncWebSocketClient * client,
               AwsEventType           type,
               void                  * arg,
               uint8_t               * data,
               size_t                  len) {
  switch (type) {
    case WS_EVT_CONNECT: {
      statConnects++;
      Serial.printf("[WS %u] CONNECT %s\n", client->id(), client->remoteIP().toString().c_str());
      client->text("{\"hello\":1}");
      broadcastSample();
      updateLed();
      break; }
    case WS_EVT_DISCONNECT: {
      statDisconnects++;
      Serial.printf("[WS %u] DISCONNECT\n", client->id());
      updateLed();
      break; }
    case WS_EVT_DATA: {
      // simple text commands
      AwsFrameInfo * info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        if (len > 0) {
          if (data[0] == '1') broadcastSample();
          else if (data[0] == 'p') client->text("pong");
        }
      }
      break; }
    case WS_EVT_PONG: {
      statPongs++;
      break; }
    case WS_EVT_ERROR: {
      statErrors++;
      Serial.printf("[WS %u] ERROR\n", client->id());
      break; }
  }
}

// HTTP API removed (WebSocket only)

void setup() {
  Serial.begin(921600);
  delay(100);

  // Dual Mode: eigener offener AP + (optional) Verbindung ins Heim-WLAN
  WiFi.mode(WIFI_AP_STA);
  WiFi.hostname("homeofficetrainer");
  WiFi.setSleep(false);

  // Start Open AP (kein Passwort) -> SSID: homeofficetrainer  IP: 192.168.4.1
  // Passphrase secured AP (WPA2). Passwort: hotrainer
  bool apOk = WiFi.softAP("homeofficetrainer", "hotrainer");
  if (apOk) {
    Serial.println("AP gestartet: SSID=homeofficetrainer PASS=hotrainer IP=" + WiFi.softAPIP().toString());
  } else {
    Serial.println("AP Start fehlgeschlagen");
  }

  // Parallel versuchen ins bekannte WLAN zu verbinden (nicht blockierend >8s)
  Serial.print("Verbinde (optional) mit WLAN ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 8000) {
    delay(250); Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWLAN (STA) verbunden, IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nSTA Timeout – weiter nur über AP erreichbar (ws://192.168.4.1/ws)");
  }

  Serial.println("WebSocket Modus aktiv (/ws)");

  Wire.begin();
  inaReady = INA226::begin();
  if (inaReady) {
    Serial.println("INA226 initialisiert");
  } else {
    Serial.println("INA226 Fehler - Messungen werden 0 sein");
  }

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();
  statWindowStart = millis();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // ensure off initially
}

void loop() {
  // Periodic RSSI log every 15s
  unsigned long now = millis();
  if (now - lastRssiLog > 15000) {
    lastRssiLog = now;
    Serial.print("RSSI: "); Serial.println(WiFi.RSSI());
  }
  // Heartbeat broadcast so client doesn't wait forever for first push
  if (now - lastWsBroadcast > WS_BROADCAST_INTERVAL_MS) {
    lastWsBroadcast = now;
    if (ws.count() > 0) broadcastSample();
  }
  // Periodic stats every 10s
  if (now - lastStatPrint > 10000) {
    unsigned long windowMs = now - statWindowStart;
    float bps = windowMs ? (1000.0f * statBroadcastsWindow / windowMs) : 0.0f;
    Serial.printf("[STAT] up=%lus con=%lu disc=%lu err=%lu bcast=%lu (%.2f/s) inaFail=%lu slowIna=%lu heap=%u lastP=%.3fV=%.3f I=%.3f pong=%lu clients=%u\n",
      now/1000UL, statConnects, statDisconnects, statErrors, statBroadcasts, bps, statInaFail, slowInaCount, ESP.getFreeHeap(), lastPower, lastVoltage, lastCurrent, statPongs, ws.count());
    lastStatPrint = now;
    statBroadcastsWindow = 0;
    statWindowStart = now;
  }
  // no blocking delay needed; yield implicitly
}
