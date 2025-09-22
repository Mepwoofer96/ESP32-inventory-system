#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include "SPIFFS.h"

const char* ssid = "Inventory_ESP";
const char* password = "Password?";

WebServer server(80);

#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCLK 18
#define SD_CS   5
#define BLINK   2

void handleRoot() {
  File file = SPIFFS.open("/index.html", "r");
  if (file) {
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "File Not Found");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[*] Creating AP");

  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);

  pinMode(BLINK, OUTPUT);

  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card MOUNT FAIL");
  } else {
    Serial.println("SD Card Mounted Successfully");
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  IPAddress local_ip(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_ip, gateway, subnet);

  Serial.print("[+] AP Created with IP Gateway ");
  Serial.println(WiFi.softAPIP());

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  int n = WiFi.softAPgetStationNum();

  if (n > 0) {
    digitalWrite(BLINK, HIGH);
  } else {
    digitalWrite(BLINK, LOW);
  }

  server.handleClient();
}
