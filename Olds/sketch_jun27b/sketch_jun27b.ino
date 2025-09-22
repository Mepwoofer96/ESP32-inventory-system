#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

#define AP_SSID "esp-port"
#define AP_PASS "12345"

AsyncWebServer server(80);

void setup() {
  Serial.begin(115200);

  // Start Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println("Access Point started");
  Serial.println(WiFi.softAPIP());

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  // Serve uploaded files
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<form method='POST' action='/upload' enctype='multipart/form-data'>"
                  "<input type='file' name='upload'><input type='submit' value='Upload'></form><br>";
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file) {
      html += String(file.name()) + "<br>";
      file = root.openNextFile();
    }
    request->send(200, "text/html", html);
  });

  // Handle file upload
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Upload Success!");
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data,
        size_t len, bool final){
    if (!index){
      Serial.printf("UploadStart: %s\n", filename.c_str());
      request->_tempFile = SPIFFS.open("/" + filename, "w");
    }
    if (len){
      request->_tempFile.write(data, len);
    }
    if (final){
      Serial.printf("UploadEnd: %s (%u)\n", filename.c_str(), index+len);
      request->_tempFile.close();
    }
  });

  // Serve static files from SPIFFS
  server.serveStatic("/files", SPIFFS, "/");

  server.begin();
}

void loop() {
  // nothing needed here
}
