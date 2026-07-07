//  Inventory.ino
//
//  Light it Up LLC
//
//  This is made for the inventory system for Light it Up LLC as a electronic inventory system that compiles and simplifies invenntory managment
//
//
//  Hardware used
//
//  NFC tags, ESP32 microcontroller, HW-125 SD Card Adapter, Possibly NFC writer
//
//
//  How it works:
//
//  Scan a NFC tag to lead you to a website hosted on the ESP32 that shows the given item you scanned. You can change the amount and show a pdf of the spec sheet of the chosen item.
//
//
//  Additions
//  Over the air updates (done OTAU), infinte scalability(done), PDF export( done), Automatic Renewal (when stock is low) (not sure how to implement yet )
//
//
//

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// RFID card (MFRC522v2 - actively maintained fork with custom-SPI-bus support)
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>
#include <vector>


//OTA updates
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>

const char* firmwareVersionURL = "https://raw.githubusercontent.com/Mepwoofer96/ESP32-inventory-system/main/version.txt";
const char* firmwareBinURL = "https://raw.githubusercontent.com/Mepwoofer96/ESP32-inventory-system/main/inventory/inventory.ino.bin";
const char* currentFirmwareVersion = "0.38.1";
bool otaCheckRequested = false;

// Wifi and AP settings

const char* apssid = "Inventory_ESP";
const char* appassword = "Password?";
const char* fake_hostname = "inventory";

bool wifiRequested = false;

bool apRequested = false;
bool serverStarted = false;
bool newAdmin = false;

String wifiSSID = "";
String wifiPass = "";

AsyncWebServer* server = nullptr;

//for dns lying
DNSServer dnsServer;

IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

// For getting the correct part when asked on html
String currentPartName = "";
String cachedName = "";
String cachedCount = "";
String cssCache = "";

//captive portal
bool portalAccepted = false;

// Rfid
#define RFID_CS_PIN 26
#define RFID_RST_PIN 27
#define RFID_H_MISO 12
#define RFID_H_MOSI 13
#define RFID_H_CLK  14

SPIClass hspi(HSPI);
MFRC522DriverPinSimple ss_pin(RFID_CS_PIN);
MFRC522DriverSPI driver{ss_pin, hspi};
MFRC522 mfrc522{driver};



bool nfcWriteRequested = false;
String nfcWriteName = "";
String nfcWriteStatus = "idle";  // idle, waiting, success, failed

// RFID and SD helper making both run without corrupting eachother
SemaphoreHandle_t spiMutex;


void initRFID() {
  hspi.begin(RFID_H_CLK, RFID_H_MISO, RFID_H_MOSI, RFID_CS_PIN);
  mfrc522.PCD_Init();
  Serial.println("RFID reader initialized");
}

// Builds and writes an NDEF URI record pointing at your inventory site
bool writeNDEFUrl(const String& partName) {
  String urlSuffix = "inventory.local/part?name=" + partName;  // goes after "http://"
  uint8_t idCode = 0x03;                                       // NDEF URI abbreviation code for "http://"

  std::vector<uint8_t> record;
  record.push_back(0xD1);                    // header: MB=1,ME=1,SR=1,TNF=well-known
  record.push_back(0x01);                    // type length
  record.push_back(1 + urlSuffix.length());  // payload length
  record.push_back(0x55);                    // type = 'U' (URI)
  record.push_back(idCode);
  for (size_t i = 0; i < urlSuffix.length(); i++) record.push_back((uint8_t)urlSuffix[i]);

  std::vector<uint8_t> tlv;
  tlv.push_back(0x03);  // NDEF Message TLV
  tlv.push_back(record.size());
  for (auto b : record) tlv.push_back(b);
  tlv.push_back(0xFE);  // Terminator TLV
  while (tlv.size() % 4 != 0) tlv.push_back(0x00);

  // Capability container — required so phones recognize this as NDEF-formatted
  uint8_t cc[4] = { 0xE1, 0x10, 0x06, 0x00 };  // 0x06 = NTAG213; use 0x3E for NTAG215, 0x6D for NTAG216
  mfrc522.MIFARE_Ultralight_Write(3, cc, 4);

  uint8_t page = 4;
  for (size_t i = 0; i < tlv.size(); i += 4) {
    uint8_t buf[4] = { tlv[i], tlv[i + 1], tlv[i + 2], tlv[i + 3] };
    MFRC522::StatusCode status = mfrc522.MIFARE_Ultralight_Write(page, buf, 4);
    if (status != MFRC522::StatusCode::STATUS_OK) {
      Serial.println("Write failed at page " + String(page) + ": " + String(MFRC522Debug::GetStatusCodeName(status)));
      return false;
    }
    page++;
  }
  return true;
}


// --- Firmware (.bin) update ---
bool performFirmwareUpdate() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, firmwareBinURL);
  http.setTimeout(20000);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.println("Firmware fetch failed, code: " + String(httpCode));
    http.end();
    client.stop();
    return false;
  }

  int contentLength = http.getSize();
  Serial.println("Firmware size reported: " + String(contentLength));
  Serial.println("Free sketch space (OTA partition): " + String(ESP.getFreeSketchSpace()));

  if (contentLength <= 0 || !Update.begin(contentLength)) {
    Serial.println("Not enough space, or bad content length: " + String(contentLength));
    http.end();
    client.stop();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buff[1024];
  size_t totalWritten = 0;
  unsigned long lastDataTime = millis();

  while (http.connected() && (totalWritten < (size_t)contentLength)) {
    size_t availableBytes = stream->available();
    if (availableBytes > 0) {
      size_t toRead = min(availableBytes, sizeof(buff));
      int bytesRead = stream->readBytes(buff, toRead);
      if (bytesRead > 0) {
        size_t written = Update.write(buff, bytesRead);
        totalWritten += written;
        lastDataTime = millis();
      }
    } else {
      if (millis() - lastDataTime > 10000) {
        Serial.println("Stream stalled, aborting");
        break;
      }
      delay(1);
    }
  }

  Serial.println("Bytes written: " + String(totalWritten) + " / " + String(contentLength));

  bool success = false;
  if (totalWritten == (size_t)contentLength && Update.end()) {
    Serial.println("Update successful, rebooting...");
    success = true;
  } else {
    Update.abort();
    Serial.println("Update failed: " + String(Update.getError()));
  }

  http.end();
  client.stop();

  if (success) {
    ESP.restart();
  }
  return false;
}

// --- Asset (HTML/CSS) update — now takes a shared client instead of making its own ---
bool downloadFileToSD(WiFiClientSecure& client, const char* url, const char* sdPath) {
  HTTPClient http;
  http.begin(client, url);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.println("Download failed: " + String(httpCode));
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  String tempPath = String(sdPath) + ".tmp";
  bool ok = false;

  if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
    File file = SD.open(tempPath, FILE_WRITE);
    if (!file) {
      xSemaphoreGive(spiMutex);
      http.end();
      return false;
    }

    int written = http.writeToStream(&file);
    file.close();

    if (written < 0) {
      Serial.println("Download error (" + String(written) + "), keeping old " + String(sdPath));
      SD.remove(tempPath);
    } else if (contentLength > 0 && written != contentLength) {
      Serial.println("Download incomplete (" + String(written) + "/" + String(contentLength) + "), keeping old " + String(sdPath));
      SD.remove(tempPath);
    } else {
      SD.remove(sdPath);
      SD.rename(tempPath, sdPath);
      Serial.println("Updated: " + String(sdPath));
      ok = true;
    }
    xSemaphoreGive(spiMutex);
  }

  http.end();
  return ok;
}

// --- Overall OTA check ---
void performOTACheck() {
  Serial.printf("Free heap at OTA start: %d, largest free block: %d\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  String remoteVersion = "";
  bool versionCheckOK = false;

  for (int attempt = 1; attempt <= 2 && !versionCheckOK; attempt++) {
    WiFiClientSecure versionClient;
    versionClient.setInsecure();
    HTTPClient http;
    http.begin(versionClient, firmwareVersionURL);
    int code = http.GET();

    if (code == HTTP_CODE_OK) {
      remoteVersion = http.getString();
      remoteVersion.trim();
      versionCheckOK = true;
      Serial.println("Remote version: " + remoteVersion);
    } else {
      Serial.println("Version check attempt " + String(attempt) + " failed, code: " + String(code));
      if (attempt < 2) delay(1500);
    }
    http.end();
    versionClient.stop();
  }

  bool firmwareAttempted = false;

  if (versionCheckOK && remoteVersion != currentFirmwareVersion) {
    firmwareAttempted = true;
    bool success = false;
    for (int attempt = 1; attempt <= 2 && !success; attempt++) {
      Serial.println("Firmware attempt " + String(attempt));
      success = performFirmwareUpdate();
      if (!success && attempt < 2) delay(2000);
    }
    if (!success) {
      Serial.println("Firmware update failed after retries, continuing without it");
    }
  } else if (versionCheckOK) {
    Serial.println("Already up to date");
  }

  if (firmwareAttempted && ESP.getMaxAllocHeap() < 40000) {
    Serial.println("Heap too low after firmware attempt, skipping asset sync this cycle");
    return;
  }

  // Shared client reused across all three asset downloads
  WiFiClientSecure assetClient;
  assetClient.setInsecure();

  downloadFileToSD(assetClient, "https://raw.githubusercontent.com/Mepwoofer96/ESP32-inventory-system/main/inventory/htmls/admin.html", "/htmls/admin.html");
  delay(500);
  downloadFileToSD(assetClient, "https://raw.githubusercontent.com/Mepwoofer96/ESP32-inventory-system/main/inventory/htmls/style.css", "/htmls/style.css");
  delay(500);
  downloadFileToSD(assetClient, "https://raw.githubusercontent.com/Mepwoofer96/ESP32-inventory-system/main/inventory/htmls/index.html", "/htmls/index.html");

  assetClient.stop();
  loadFileCache();
}

String escapePDFText(String s) {
  s.replace("\\", "\\\\");
  s.replace("(", "\\(");
  s.replace(")", "\\)");
  return s;
}


void loadFileCache() {
  if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
    File f = SD.open("/htmls/style.css", "r");
    if (f) {
      while (f.available()) cssCache += (char)f.read();
      f.close();
      Serial.println("CSS cached");
    }
    xSemaphoreGive(spiMutex);
  }
}

void loadPartCache(const String& name) {
  if (cachedName == name) return;
  if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
    File file = SD.open("/data/parts.csv", "r");
    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();
      int commaIndex = line.indexOf(',');
      String n = line.substring(0, commaIndex);
      if (n == name) {
        cachedName = n;
        cachedCount = line.substring(commaIndex + 1);
        break;
      }
    }
    file.close();
    xSemaphoreGive(spiMutex);
  }
}

// PROCESSOR
// handles all %VARS% in htmls
String processor(const String& var) {
  // If var is %PART_LIST% used in index to show all current inventory options
  if (var == "PART_LIST") {
    String output = "";
    if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
      File file = SD.open("/data/parts.csv", "r");
      if (!file) {
        xSemaphoreGive(spiMutex);
        return "<tr><td>No parts found</td></tr>";
      }
      while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        int commaIndex = line.indexOf(',');
        String name = line.substring(0, commaIndex);
        String count = line.substring(commaIndex + 1);
        output += "<tr>";
        output += "<td><a href='/part?name=" + name + "'>" + name + "</a></td>";
        output += "<td><span class='stock'>" + count + "</span></td>";
        output += "<td><a href='/pdfs/" + name + ".pdf'>View</a></td>";
        output += "</tr>";
      }
      file.close();
      xSemaphoreGive(spiMutex);
    }
    return output;
  }
  // If var is %PART_NAME% handles requests for specific part names on the parts html page
  if (var == "PART_NAME") {
    return currentPartName;
  }
  // If var is %PART_COUNT% handles requests for the count of parts on the parts html page
  if (var == "PART_COUNT") {
    loadPartCache(currentPartName);
    return cachedCount;
  }
  // Same as above two but for the photo
  if (var == "PART_PHOTO") {
    return "/photos/" + currentPartName + ".jpg";
  }
  // same as above but for pdf
  if (var == "PART_PDF") {
    return "/pdfs/" + currentPartName + ".pdf";
  }

  if (var == "FIRMWARE_VERSION") {
    return String(currentFirmwareVersion);
  }

  return String();
}


// SD Card initalization
void initSDCard() {
  SPI.begin(18, 19, 23, 5);  // SCK, MISO, MOSI, CS
  if (!SD.begin(5, SPI, 8000000)) {
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }
  Serial.println("SD Card initialized");
}

// admin stuff

String adminUser = "admin";
String adminPass = "password?";


// Start pages including pages that send data
void initRoutes() {

  server = new AsyncWebServer(80);
  // index page
  server->on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (WiFi.getMode() == WIFI_AP && !portalAccepted) {
      request->send(SD, "/htmls/portal_gate.html", "text/html", false, processor);
    } else {
      request->send(SD, "/htmls/index.html", "text/html", false, processor);
    }
  });

  // part specific page
  server->on("/part", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (request->hasParam("name")) {
      currentPartName = request->getParam("name")->value();
    }
    request->send(SD, "/htmls/part.html", "text/html", false, processor);
  });

  // admin pageString name = currentPartName
  server->on("/admin", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str())) {
      return request->requestAuthentication();
    }
    request->send(SD, "/htmls/admin.html", "text/html", false, processor);
  });

  //for dns lying
  // add part — appends to CSV and saves uploaded files
  server->on(
    "/addpart", HTTP_POST, [](AsyncWebServerRequest* request) {
      if (!request->authenticate(adminUser.c_str(), adminPass.c_str())) {
        return request->requestAuthentication();
      }
      if (request->hasParam("name", true) && request->hasParam("count", true)) {
        String name = request->getParam("name", true)->value();
        String count = request->getParam("count", true)->value();

        if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
          File csv = SD.open("/data/parts.csv", FILE_APPEND);
          if (csv) {
            csv.println(name + "," + count);
            csv.close();
          }
          xSemaphoreGive(spiMutex);
        }
        request->redirect("/writetag?name=" + name);
      } else {
        request->send(400, "text/plain", "Missing name or count");
      }
    },

    [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
      // runs as files come in
      if (!request->hasParam("name", true)) return;
      String partName = request->getParam("name", true)->value();

      if (filename.endsWith(".pdf")) {
        String path = "/pdfs/" + partName + ".pdf";
        if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
          if (index == 0) request->_tempFile = SD.open(path, FILE_WRITE);
          if (request->_tempFile) request->_tempFile.write(data, len);
          if (final) request->_tempFile.close();
          xSemaphoreGive(spiMutex);
        }
      }
      if (filename.endsWith(".jpg")) {
        String path = "/photos/" + partName + ".jpg";
        if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
          if (index == 0) request->_tempFile = SD.open(path, FILE_WRITE);
          if (request->_tempFile) request->_tempFile.write(data, len);
          if (final) request->_tempFile.close();
          xSemaphoreGive(spiMutex);
        }
      }
    });

  // write nfc tag
  server->on("/writetag", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str())) {
      return request->requestAuthentication();
    }
    if (request->hasParam("name")) {
      currentPartName = request->getParam("name")->value();
    }
    request->send(SD, "/htmls/writetag.html", "text/html", false, processor);
  });

  server->on("/deletepart", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (request->hasParam("name", true)) {
      String nameToDelete = request->getParam("name", true)->value();
      bool ok = true;

      if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
        File csv = SD.open("/data/parts.csv", "r");
        if (!csv) {
          xSemaphoreGive(spiMutex);
          request->send(500, "text/plain", "Could not open CSV");
          return;
        }

        String newCSV = "";
        while (csv.available()) {
          String line = csv.readStringUntil('\n');
          line.trim();
          if (line.length() == 0) continue;
          int commaIndex = line.indexOf(',');
          String name = line.substring(0, commaIndex);
          if (name != nameToDelete) {
            newCSV += line + "\n";
          } else {
            Serial.println("Deleting: " + name);
          }
        }
        csv.close();

        File out = SD.open("/data/parts.csv", FILE_WRITE);
        if (!out) {
          xSemaphoreGive(spiMutex);
          request->send(500, "text/plain", "Could not write CSV");
          return;
        }
        out.print(newCSV);
        out.close();

        SD.remove("/pdfs/" + nameToDelete + ".pdf");
        SD.remove("/photos/" + nameToDelete + ".jpg");

        xSemaphoreGive(spiMutex);
      } else {
        ok = false;
      }

      request->send(ok ? 200 : 500, "text/plain", ok ? "Part deleted" : "Busy, try again");
    } else {
      request->send(400, "text/plain", "Missing name");
    }
  });


  server->on("/updatecount", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (request->hasParam("name", true) && request->hasParam("count", true)) {
      String name = request->getParam("name", true)->value();
      String newCount = request->getParam("count", true)->value();

      if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
        File csv = SD.open("/data/parts.csv", "r");
        String newCSV = "";
        while (csv.available()) {
          String line = csv.readStringUntil('\n');
          line.trim();
          if (line.length() == 0) continue;
          int commaIndex = line.indexOf(',');
          String lineName = line.substring(0, commaIndex);
          if (lineName == name) {
            newCSV += name + "," + newCount + "\n";
          } else {
            newCSV += line + "\n";
          }
        }
        csv.close();

        File out = SD.open("/data/parts.csv", FILE_WRITE);
        out.print(newCSV);
        out.close();

        xSemaphoreGive(spiMutex);
      }

      request->send(200, "text/plain", "OK");
    }
  });


  server->on("/setwifi", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
      wifiSSID = request->getParam("ssid", true)->value();
      wifiPass = request->getParam("password", true)->value();
      wifiRequested = true;
      request->send(200, "text/plain", "Connecting to WiFi...");
    }
  });

  server->on("/setap", HTTP_POST, [](AsyncWebServerRequest* request) {
    apRequested = true;
    request->send(200, "text/plain", "Switching to AP mode...");
  });

  server->on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/css", cssCache);
  });

  // Writing to a NFC tag
  server->on("/internalnfc", HTTP_POST, [](AsyncWebServerRequest* request) {
    Serial.println("requested internal nfc");
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str())) {
      return request->requestAuthentication();
    }
    if (currentPartName.length() == 0) {
      request->send(400, "text/plain", "No part selected");
      return;
    }
    nfcWriteName = currentPartName;
    nfcWriteStatus = "waiting";
    nfcWriteRequested = true;
    request->send(200, "text/plain", "Tap the tag now...");
  });

  server->on("/nfcstatus", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", nfcWriteStatus);
  });

  //generates a PDF report of the parts.csv inventory
  server->on("/printpdf", HTTP_GET, [](AsyncWebServerRequest* request) {
    String pdf;
    bool ok = false;

    if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
      File csv = SD.open("/data/parts.csv", "r");
      if (!csv) {
        xSemaphoreGive(spiMutex);
        request->send(404, "text/plain", "No inventory data found");
        return;
      }

      int col1X = 50;   // Part Number column
      int col2X = 320;  // Count column
      int y = 740;
      int lineHeight = 20;

      String content = "";

      // Title
      content += "BT /F2 22 Tf 50 770 Td (Inventory Report) Tj ET\n";

      // Header row (bold)
      content += "BT /F2 12 Tf " + String(col1X) + " " + String(y) + " Td (Part Number) Tj ET\n";
      content += "BT /F2 12 Tf " + String(col2X) + " " + String(y) + " Td (Count) Tj ET\n";

      // Divider line under header
      y -= 8;
      content += String(col1X) + " " + String(y) + " m 550 " + String(y) + " l S\n";
      y -= lineHeight;

      while (csv.available()) {
        String line = csv.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int commaIndex = line.indexOf(',');
        String name = escapePDFText(line.substring(0, commaIndex));
        String count = escapePDFText(line.substring(commaIndex + 1));

        content += "BT /F1 12 Tf " + String(col1X) + " " + String(y) + " Td (" + name + ") Tj ET\n";
        content += "BT /F1 12 Tf " + String(col2X) + " " + String(y) + " Td (" + count + ") Tj ET\n";
        y -= lineHeight;

        if (y < 50) break;  // stop before running off the page
      }
      csv.close();
      xSemaphoreGive(spiMutex);

      // --- assemble the PDF (no SD access below, safe outside the lock) ---
      pdf = "%PDF-1.4\n";
      pdf += "1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj\n";
      pdf += "2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj\n";
      pdf += "3 0 obj<</Type/Page/Parent 2 0 R/Resources<</Font<</F1 5 0 R/F2 6 0 R>>>>/MediaBox[0 0 612 792]/Contents 4 0 R>>endobj\n";
      pdf += "4 0 obj<</Length " + String(content.length()) + ">>stream\n";
      pdf += content;
      pdf += "endstream endobj\n";
      pdf += "5 0 obj<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>endobj\n";
      pdf += "6 0 obj<</Type/Font/Subtype/Type1/BaseFont/Helvetica-Bold>>endobj\n";
      int xrefStart = pdf.length();
      pdf += "xref\n0 7\n0000000000 65535 f \n";
      pdf += "trailer<</Size 7/Root 1 0 R>>\n";
      pdf += "startxref\n" + String(xrefStart) + "\n%%EOF";
      ok = true;
    }

    if (!ok) {
      request->send(500, "text/plain", "Busy, try again");
      return;
    }

    AsyncWebServerResponse* response = request->beginResponse(200, "application/pdf", pdf);
    response->addHeader("Content-Disposition", "attachment; filename=inventory.pdf");
    request->send(response);
  });

  server->on("/setadmin", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (request->hasParam("username", true) && request->hasParam("password", true)) {
      adminUser = request->getParam("username", true)->value();
      adminPass = request->getParam("password", true)->value();
      newAdmin = true;
      request->send(200, "text/plain", "Changed Admin User (REMEMBER INFO will reset on power off)");
    }
  });

  // Manual, mutex-protected PDF/photo serving — replaces serveStatic(), which
  // has no hook to lock around, so it could still collide with RFID reads.
  server->on("/pdfs/*", HTTP_GET, [](AsyncWebServerRequest* request) {
    String path = request->url();
    if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
      if (SD.exists(path)) {
        request->send(SD, path, "application/pdf");
      } else {
        request->send(404);
      }
      xSemaphoreGive(spiMutex);
    } else {
      request->send(500, "text/plain", "Busy, try again");
    }
  });

  server->on("/photos/*", HTTP_GET, [](AsyncWebServerRequest* request) {
    String path = request->url();
    if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
      if (SD.exists(path)) {
        request->send(SD, path, "image/jpeg");
      } else {
        request->send(404);
      }
      xSemaphoreGive(spiMutex);
    } else {
      request->send(500, "text/plain", "Busy, try again");
    }
  });

  // Captive portals
  // Android captive portal check
  server->on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (portalAccepted) {
      request->send(204);  // exactly what Android expects from a validated network
    } else {
      request->redirect("http://192.168.4.1/");
    }
  });

  server->on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (portalAccepted) {
      request->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    } else {
      request->redirect("http://192.168.4.1/");
    }
  });

  server->on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (portalAccepted) {
      request->send(200, "text/plain", "Microsoft Connect Test");
    } else {
      request->redirect("http://192.168.4.1/");
    }
  });

  server->onNotFound([](AsyncWebServerRequest* request) {
    request->redirect("http://192.168.4.1/");
  });


  // UPDATE STUFF
  // Route to trigger a check + update, admin-gated
  server->on("/checkupdate", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str())) {
      return request->requestAuthentication();
    }
    request->send(200, "text/plain", "Checking for updates...");
    otaCheckRequested = true;  // defer the actual work to loop()
  });

  //check for online
  server->on("/wifistatus", HTTP_GET, [](AsyncWebServerRequest* request) {
    bool online = (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED);
    request->send(200, "text/plain", online ? "online" : "offline");
  });

  server->on("/enterportal", HTTP_GET, [](AsyncWebServerRequest* request) {
    portalAccepted = true;
    request->send(200, "text/html", "<html><body>Connecting...<script>window.location.href='http://192.168.4.1/';</script></body></html>");
  });
}

//For AP mode
void initAP() {
  MDNS.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(200);

  WiFi.mode(WIFI_AP);
  delay(100);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(apssid, appassword);
  dnsServer.start(53, "*", apIP);
  portalAccepted = false;

  if (!serverStarted) {
    server->begin();
    serverStarted = true;
    Serial.println("Server started");
  }
}


// For Wifi mode
void initWiFi() {
  dnsServer.stop();
  MDNS.end();

  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(200);  // let the driver fully tear down before switching modes

  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(1000);
    attempts++;
  }
  if (WiFi.status() != WL_CONNECTED) {
    apRequested = true;
    return;
  }
  if (!serverStarted) {
    server->begin();
    serverStarted = true;
  }
  Serial.println(WiFi.localIP());
  if (MDNS.begin("inventory")) {
    Serial.println("mDNS started — access at http://inventory.local");
  }
}

void setup() {

  Serial.begin(115200);
  delay(1000);

  // create the mutex BEFORE anything tries to use it
  spiMutex = xSemaphoreCreateMutex();

  //use sd
  initSDCard();

  delay(500);

  initRFID();
  MFRC522Debug::PCD_DumpVersionToSerial(mfrc522, Serial);

  loadFileCache();

  // Setup pages
  initRoutes();

  // ALWAYS BOOT INTO AP MODE FIRST
  //initalize ap
  initAP();
  Serial.println("AP done");
}

void loop() {
  dnsServer.processNextRequest();

  if (wifiRequested) {
    // Will fall back if wifi is not contacted
    wifiRequested = false;
    initWiFi();
  }

  if (apRequested) {
    apRequested = false;
    initAP();
  }

  if (otaCheckRequested) {
    otaCheckRequested = false;
    performOTACheck();
  }

  if (nfcWriteRequested) {
    if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
      if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        bool ok = writeNDEFUrl(nfcWriteName);
        nfcWriteStatus = ok ? "success" : "failed";
        nfcWriteRequested = false;
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
      }
      xSemaphoreGive(spiMutex);
    }
  }
}
