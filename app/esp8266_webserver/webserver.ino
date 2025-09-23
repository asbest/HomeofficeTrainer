#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// WLAN Zugangsdaten
const char* ssid = "Fritzi";
const char* password = "65770551205628807473";

// Webserver auf Port 80
ESP8266WebServer server(80);

// Pin für den analogen Eingang
const int analogPin = A0;

// Hilfsfunktion: JSON ausgeben
void handleApi() {
  Serial.println("Handle request");
  int sensorValue = analogRead(analogPin);

  // Beispiel: Umrechnung in Spannung (bei 10-bit ADC, 0–1023, 0–1V Referenz bei ESP8266)
  float voltage = sensorValue * (1.0 / 1023.0);

  // JSON-Response aufbauen
  String json = "{";
  json += "\"power\":" + String(sensorValue) + ",";
  json += "\"voltage\":" + String(voltage, 3);
  json += "}";

  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(9600);
  delay(100);

  // WLAN verbinden
  Serial.println();
  Serial.print("Verbinde mit WLAN: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WLAN verbunden");
  Serial.print("IP Adresse: ");
  Serial.println(WiFi.localIP());

  // API Endpoint registrieren
  server.on("/api/power", handleApi);

  // Server starten
  server.begin();
  Serial.println("HTTP Server gestartet");
}

void loop() {
  server.handleClient();
}
