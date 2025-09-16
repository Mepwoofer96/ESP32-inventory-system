#include <WiFi.h>
const char* ssid = "YourESP32AP";
const char* password = "YourAPPassword";

        void setup() {
      Serial.begin(115200);
      Serial.println("\n[*] Creating AP");

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
