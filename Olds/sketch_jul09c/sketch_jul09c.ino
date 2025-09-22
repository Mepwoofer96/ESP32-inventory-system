#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <SD.h>

// SPI pin mapping
#define SD_CS    5
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23

// WiFi AP credentials
const char* ssid = "ESP32_FileServer";
const char* password = "12345678";

AsyncWebServer server(80);
File uploadFile;

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Start WiFi Access Point
  Serial.println("Starting WiFi Access Point...");
  if (!WiFi.softAP(ssid, password)) {
    Serial.println("Failed to start Access Point");
    return;
  }
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // Init SD card with custom SPI
  Serial.println("Initializing SD card...");
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI)) {
    Serial.println("SD card mount failed!");
    return;
  }
  Serial.println("SD card initialized");

  // Web UI at /file
  server.on("/file", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<h2>ESP32 File Manager</h2><form method='POST' action='/upload' enctype='multipart/form-data'>"
                  "<input type='file' name='file'><input type='submit' value='Upload'></form><hr>";

    File root = SD.open("/");
    File file = root.openNextFile();
    while (file) {
      html += "<p><a href='/download?name=" + String(file.name()) + "'>" + String(file.name()) + "</a> "
              "<a href='/delete?name=" + String(file.name()) + "'>[Delete]</a></p>";
      file = root.openNextFile();
    }
    request->send(200, "text/html", html);
  });

  // Upload endpoint
  server.on("/upload", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      request->send(200, "text/plain", "Upload finished");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (!index) {
        Serial.printf("UploadStart: %s\n", filename.c_str());
        uploadFile = SD.open("/" + filename, FILE_WRITE);
      }
      if (uploadFile) {
        uploadFile.write(data, len);
        if (final) {
          uploadFile.close();
          Serial.printf("UploadEnd: %s (%u bytes)\n", filename.c_str(), index + len);
        }
      }
    });

  // Download endpoint
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("name")) {
      request->send(400, "text/plain", "Missing filename");
      return;
    }
    String filename = "/" + request->getParam("name")->value();
    if (SD.exists(filename)) {
      request->send(SD, filename, "application/octet-stream");
    } else {
      request->send(404, "text/plain", "File not found");
    }
  });

  // Delete endpoint
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("name")) {
      request->send(400, "text/plain", "Missing filename");
      return;
    }
    String filename = "/" + request->getParam("name")->value();
    if (SD.remove(filename)) {
      request->redirect("/file");
    } else {
      request->send(500, "text/plain", "Failed to delete file");
    }
  });

  server.onNotFound(notFound);
  server.begin();
  Serial.println("Web server started");
}

void loop() {}
