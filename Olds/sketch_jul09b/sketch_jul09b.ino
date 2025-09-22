#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <SD.h>

// SPI pin definitions
#define SD_CS    5
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23

const char* ssid = "The";
const char* password = "12345678";

AsyncWebServer server(80);
File uploadFile;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Start Wi-Fi Access Point
  Serial.println("Starting WiFi AP...");
  bool apStarted = WiFi.softAP(ssid, password);
  if (!apStarted) {
    Serial.println("Failed to start Access Point!");
    return;
  }
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // Initialize SD card with custom SPI
  Serial.println("Initializing SD card...");
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI)) {
    Serial.println("SD card initialization failed!");
    return;
  }
  Serial.println("SD card initialized.");

  // Web server routes
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

  server.on("/upload", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      request->send(200, "text/plain", "File Uploaded");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (!index) {
        uploadFile = SD.open("/" + filename, FILE_WRITE);
      }
      if (uploadFile) {
        uploadFile.write(data, len);
        if (final) {
          uploadFile.close();
        }
      }
    });

  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("name")) {
      request->send(400, "text/plain", "Missing filename");
      return;
    }
    String filename = request->getParam("name")->value();
    if (SD.exists("/" + filename)) {
      request->send(SD, "/" + filename, "application/octet-stream");
    } else {
      request->send(404, "text/plain", "File not found");
    }
  });

  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("name")) {
      request->send(400, "text/plain", "Missing filename");
      return;
    }
    String filename = request->getParam("name")->value();
    if (SD.remove("/" + filename)) {
      request->redirect("/file");
    } else {
      request->send(500, "text/plain", "File deletion failed");
    }
  });

  server.begin();
  Serial.println("Web server started");
    // New route to preview content on /inv
  server.on("/inv", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<h2>Inventory Viewer</h2><p><a href='/file'>Back to File Manager</a></p><hr>";

    File root = SD.open("/");
    File file = root.openNextFile();
    while (file) {
      String fname = String(file.name());
      fname.replace("/", ""); // remove leading slash for display
      html += "<p><strong>" + fname + "</strong><br>";

      if (fname.endsWith(".jpg") || fname.endsWith(".jpeg") || fname.endsWith(".png") || fname.endsWith(".gif")) {
        html += "<img src='/download?name=" + fname + "' style='max-width:300px'><br>";
      } else if (fname.endsWith(".pdf")) {
        html += "<iframe src='/download?name=" + fname + "' width='100%' height='400px'></iframe><br>";
      } else {
        html += "<a href='/download?name=" + fname + "'>Download</a><br>";
      }

      html += "</p><hr>";
      file = root.openNextFile();
    }

    request->send(200, "text/html", html);
  });

}


void loop() {}
