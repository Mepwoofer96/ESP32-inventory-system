#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
const char* ssid = "Inventory_ESP";
const char* password = "Password?";
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCLK 18
#define SD_CS 5

        void setup() {
      Serial.begin(115200);
      Serial.println("\n[*] Creating AP");
      SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
      if (!SD.begin(SD_CS)) {
        Serial.println("SD Card MOUNT FAIL");
      } else {
        Serial.println("SD Card Mounted Successfully");
      }
      WiFi.mode(WIFI_AP);
      WiFi.softAP(ssid, password);

      // Optional: Configure static IP for the AP
      // IPAddress local_ip(192, 168, 4, 1);
      // IPAddress gateway(192, 168, 4, 1);
      // IPAddress subnet(255, 255, 255, 0);
      // WiFi.softAPConfig(local_ip, gateway, subnet);

      Serial.print("[+] AP Created with IP Gateway ");
      Serial.println(WiFi.softAPIP());
    }

        void loop() {
      // Your application logic here, e.g., handling web server requests
    }