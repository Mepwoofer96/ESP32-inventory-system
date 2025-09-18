#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
const char* ssid = "Inventory_ESP";
const char* password = "Password?";
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCLK 18
#define SD_CS 5
#define BLINK 2;
void setup() {
  Serial.begin(115200);
  Serial.println("\n[*] Creating AP");
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  pinMode(blink, OUTPUT);
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
}

void loop() {
  int n = WiFi.softAPgetStationNum();  // number of connected clients

  if (n > 0) {
    digitalWrite(BLINK, HIGH);  // blink if at least one client
  } else {
    digitalWrite(BLINK, LOW);  // off if no clients
  }
}
