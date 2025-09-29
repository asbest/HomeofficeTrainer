#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>

// Optional: reduce max clients if memory constrained
#ifndef WEBSOCKETS_MAX_CLIENTS
#define WEBSOCKETS_MAX_CLIENTS 2
#endif

// WLAN
const char* ssid     = "Fritzi";
const char* password = "65770551205628807473";

WebSocketsServer wsServer(81); // WebSocket on port 81

const int analogPin = A0;

// Simple moving average buffer
const uint8_t AVG_SAMPLES = 8;
uint16_t sampleBuf[AVG_SAMPLES];
uint8_t sampleIdx = 0;
bool bufFilled = false;
unsigned long lastRssiLog = 0;
unsigned long lastWsBroadcast = 0; // heartbeat broadcast timer
const unsigned long WS_BROADCAST_INTERVAL_MS = 1000; // 1s push

uint16_t readAveragedRaw() {
  uint32_t acc = 0;
  for (uint8_t i = 0; i < AVG_SAMPLES; i++) {
    uint16_t v = analogRead(analogPin);
    acc += v;
    delay(2); // short settle
  }
  return acc / AVG_SAMPLES;
}

float rawToVoltage(uint16_t raw) {
  return raw * (1.0f / 1023.0f); // adjust if voltage divider used
}

void updateMoving(uint16_t raw) {
  sampleBuf[sampleIdx++] = raw;
  if (sampleIdx >= AVG_SAMPLES) { sampleIdx = 0; bufFilled = true; }
}

uint16_t movingAverageRaw() {
  if (!bufFilled) {
    // use simple current raw until buffer filled
    return sampleBuf[(sampleIdx + AVG_SAMPLES - 1) % AVG_SAMPLES];
  }
  uint32_t sum = 0;
  for (uint8_t i = 0; i < AVG_SAMPLES; i++) sum += sampleBuf[i];
  return sum / AVG_SAMPLES;
}

String buildJson(uint16_t smoothRaw, float voltage, float current) {
  String json = "{";
  json += "\"power\":" + String(smoothRaw) + ',';
  json += "\"voltage\":" + String(voltage, 3) + ',';
  json += "\"current\":" + String(current, 3);
  json += '}';
  return json;
}

void broadcastSample() {
  uint16_t raw = readAveragedRaw();
  updateMoving(raw);
  uint16_t smoothRaw = movingAverageRaw();
  float voltage = rawToVoltage(smoothRaw);
  float current = -1.0f;
  String json = buildJson(smoothRaw, voltage, current);
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

  // Prime buffer
  uint16_t initRaw = analogRead(analogPin);
  for (uint8_t i=0;i<AVG_SAMPLES;i++) sampleBuf[i]=initRaw;

  Serial.println("Nur WebSocket Modus aktiv (kein HTTP)");

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
