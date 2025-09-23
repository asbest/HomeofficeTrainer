#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// WLAN
const char* ssid     = "Fritzi";
const char* password = "65770551205628807473";

ESP8266WebServer server(80);
const int analogPin = A0;

void handleApi() {
  int sensorValue = analogRead(analogPin);
  float voltage = sensorValue * (1.0 / 1023.0);

  // JSON
  String json = "{";
  json += "\"power\":" + String(sensorValue) + ",";
  json += "\"voltage\":" + String(voltage, 3);
  json += "}";

  // wichtige Header für Browser
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.sendHeader("Connection", "close");       // verhindert zickiges Keep-Alive

  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(100);

  WiFi.mode(WIFI_STA);       // nur Client
  WiFi.setSleep(false);      // Sleep aus -> stabilere Verbindungen beim Polling
  WiFi.begin(ssid, password);

  Serial.print("Verbinde mit WLAN ");
  Serial.println(ssid);
  while (WiFi.status() != WL_CONNECTED) { delay(250); Serial.print("."); }
  Serial.println("\nWLAN verbunden, IP: " + WiFi.localIP().toString());

  server.on("/api/power", HTTP_GET, handleApi);

  // optional: vermeidet 404-Spam vom Browser
  server.on("/favicon.ico", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(204); // no content
  });

  server.begin();
  Serial.println("HTTP Server gestartet");
}

void loop() {
  server.handleClient();
  delay(10);   // Feed Watchdog & kooperatives Multitasking
}
