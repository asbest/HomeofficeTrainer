#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
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
  bool readMeasurements(float &busVoltage, float &current, float &power) {
    uint16_t vraw, craw, praw;
    if (!readRegister(REG_BUS, vraw)) return false;
    if (!readRegister(REG_CURRENT, craw)) return false;
    if (!readRegister(REG_POWER, praw)) return false;
    // Convert
    busVoltage = (vraw >> 3) * 0.004; // each bit = 1.25mV, but datasheet: Bus voltage register 13-bit (bits 15..3); LSB=1.25mV -> (vraw>>3)*0.00125; using 0.004? correct: 0.00125
    busVoltage = (vraw >> 3) * 0.00125; // correct conversion
    current = (int16_t)craw * 0.0005; // 0.5 mA per bit
    power = praw * 0.0125; // 12.5 mW per bit
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

WebSocketsServer wsServer(81); // WebSocket on port 81

// Legacy analog smoothing removed (INA226 provides filtered readings)
const int analogPin = A0; // leftover (not used with INA226 now)

// State for INA226
bool inaReady = false;
unsigned long lastRssiLog = 0;
unsigned long lastWsBroadcast = 0; // heartbeat broadcast timer
const unsigned long WS_BROADCAST_INTERVAL_MS = 500; // 0.5s push (doubled sampling rate)

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
  if (inaReady) {
    if (INA226::readMeasurements(v, c, p)) {
      // p already watts from conversion
    } else {
      Serial.println("INA226 read failed");
    }
  }
  String json = buildJson(p, v, c);
  wsServer.broadcastTXT(json);
}

void onWsEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED: {
      Serial.print("[WS] Client "); Serial.print(num); Serial.println(" disconnected");
      break;
    }
    case WStype_CONNECTED: {
      IPAddress ip = wsServer.remoteIP(num);
      Serial.print("[WS] Client "); Serial.print(num); Serial.print(" connected from "); Serial.println(ip);
      // Immediate sample
      broadcastSample();
      break;
    }
    case WStype_TEXT: {
      Serial.print("[WS] Text from "); Serial.print(num); Serial.print(": ");
      for (size_t i=0;i<length;i++) Serial.print((char)payload[i]);
      Serial.println();
      if (length && (char)payload[0] == 'p') {
        wsServer.sendTXT(num, "pong");
      } else if (length && (char)payload[0] == '1') {
        // Force immediate broadcast when client sends '1'
        broadcastSample();
      }
      break;
    }
    case WStype_BIN: {
      Serial.print("[WS] Binary length="); Serial.println(length);
      break;
    }
    case WStype_PING: {
      Serial.println("[WS] PING");
      break;
    }
    case WStype_PONG: {
      Serial.println("[WS] PONG");
      break;
    }
    case WStype_ERROR: {
      Serial.println("[WS] ERROR event");
      break;
    }
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN: {
      Serial.println("[WS] Fragmented frame event");
      break;
    }
  }
}

// HTTP API removed (WebSocket only)

void setup() {
  Serial.begin(921600);
  delay(100);

  WiFi.mode(WIFI_STA);       // nur Client
  WiFi.hostname("homeofficetrainer");
  WiFi.setSleep(false);      // Sleep aus -> stabilere Verbindungen beim Polling
  WiFi.begin(ssid, password);

  Serial.print("Verbinde mit WLAN ");
  Serial.println(ssid);
  while (WiFi.status() != WL_CONNECTED) { delay(250); Serial.print('.'); }
  Serial.println("\nWLAN verbunden, IP: " + WiFi.localIP().toString());

  Serial.println("Nur WebSocket Modus aktiv (kein HTTP)");

  Wire.begin();
  inaReady = INA226::begin();
  if (inaReady) {
    Serial.println("INA226 initialisiert");
  } else {
    Serial.println("INA226 Fehler - Messungen werden 0 sein");
  }

  wsServer.begin();
  wsServer.onEvent(onWsEvent);
}

void loop() {
  wsServer.loop();
  // Periodic RSSI log every 15s
  unsigned long now = millis();
  if (now - lastRssiLog > 15000) {
    lastRssiLog = now;
    Serial.print("RSSI: "); Serial.println(WiFi.RSSI());
  }
  // Heartbeat broadcast so client doesn't wait forever for first push
  if (now - lastWsBroadcast > WS_BROADCAST_INTERVAL_MS) {
    lastWsBroadcast = now;
    if (wsServer.connectedClients() > 0) {
      broadcastSample();
    }
  }
  delay(2); // Keep latency low
}
