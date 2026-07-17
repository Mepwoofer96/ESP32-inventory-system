//  Inventory.ino
//
//  Additions
//  Over the air updates, infinte scalability, PDF export, Automatic Renewal (when stock is low)
//
//
//
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

const char* ssid = "Inventory_ESP";
const char* password = "Password?";

AsyncWebServer server(80);
DNSServer dnsServer;
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);


String currentPartName = "";

// -----------------------------------------------
// PROCESSOR — handles all %PLACEHOLDERS% in HTML
// sits outside everything, above setup()
// -----------------------------------------------
String processor(const String& var) {
  if (var == "PART_LIST") {
    File file = SD.open("/data/parts.csv", "r");
    if (!file) {
      return "<tr><td>No parts found</td></tr>";
    }
    String output = "";
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
    return output;
  }
  if (var == "PART_NAME") {
    return currentPartName;
  }
  if (var == "PART_COUNT") {
    File file = SD.open("/data/parts.csv", "r");
    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();
      int commaIndex = line.indexOf(',');
      String name = line.substring(0, commaIndex);
      String count = line.substring(commaIndex + 1);
      if (name == currentPartName) {
        file.close();
        return count;
      }
    }
    file.close();
    return "0";
  }
  if (var == "PART_PHOTO") {
    return "/photos/" + currentPartName + ".jpg";
  }
  if (var == "PART_PDF") {
    return "/pdfs/" + currentPartName + ".pdf";
  }
  return String();
}

// -----------------------------------------------
// SD CARD INIT
// -----------------------------------------------
void initSDCard() {
  if (!SD.begin()) {
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

// -----------------------------------------------
// AP + SERVER INIT
// all server.on routes live here
// -----------------------------------------------
void initAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(ssid, password);
  dnsServer.start(53, "*", apIP);

  // index page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SD, "/htmls/index.html", "text/html", false, processor);
  });

  // part specific page
  server.on("/part", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (request->hasParam("name")) {
      currentPartName = request->getParam("name")->value();
    }
    request->send(SD, "/htmls/part.html", "text/html", false, processor);
  });

  // admin page
  server.on("/admin", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SD, "/htmls/admin.html", "text/html");
  });

  // add part — appends to CSV and saves uploaded files
  server.on(
    "/addpart", HTTP_POST,
    [](AsyncWebServerRequest* request) {
      // runs after uploads complete
      if (request->hasParam("name", true) && request->hasParam("count", true)) {
        String name = request->getParam("name", true)->value();
        String count = request->getParam("count", true)->value();

        // append new line to CSV
        File csv = SD.open("/data/parts.csv", FILE_APPEND);
        if (csv) {
          csv.println(name + "," + count);
          csv.close();
        }
        request->send(200, "text/plain", "Part added");
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
        if (index == 0) request->_tempFile = SD.open(path, FILE_WRITE);
        if (request->_tempFile) request->_tempFile.write(data, len);
        if (final) request->_tempFile.close();
      }
      if (filename.endsWith(".jpg")) {
        String path = "/photos/" + partName + ".jpg";
        if (index == 0) request->_tempFile = SD.open(path, FILE_WRITE);
        if (request->_tempFile) request->_tempFile.write(data, len);
        if (final) request->_tempFile.close();
      }
    });

  server.on("/deletepart", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (request->hasParam("name", true)) {
      String nameToDelete = request->getParam("name", true)->value();

      // read existing csv
      File csv = SD.open("/data/parts.csv", "r");
      if (!csv) {
        request->send(500, "text/plain", "Could not open CSV");
        return;
      }

      // build a new string with every line EXCEPT the one to delete
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

      // overwrite the csv with the new string
      File out = SD.open("/data/parts.csv", FILE_WRITE);
      if (!out) {
        request->send(500, "text/plain", "Could not write CSV");
        return;
      }
      out.print(newCSV);
      out.close();

      // delete the associated files
      SD.remove("/pdfs/" + nameToDelete + ".pdf");
      SD.remove("/photos/" + nameToDelete + ".jpg");

      request->send(200, "text/plain", "Part deleted");
    } else {
      request->send(400, "text/plain", "Missing name");
    }
  });

  server.on("/updatecount", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (request->hasParam("name", true) && request->hasParam("count", true)) {
      String name = request->getParam("name", true)->value();
      String newCount = request->getParam("count", true)->value();

      // read csv into memory
      File csv = SD.open("/data/parts.csv", "r");
      String newCSV = "";
      while (csv.available()) {
        String line = csv.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        int commaIndex = line.indexOf(',');
        String lineName = line.substring(0, commaIndex);
        if (lineName == name) {
          // replace this line with updated count
          newCSV += name + "," + newCount + "\n";
        } else {
          newCSV += line + "\n";
        }
      }
      csv.close();

      // write back
      File out = SD.open("/data/parts.csv", FILE_WRITE);
      out.print(newCSV);
      out.close();

      request->send(200, "text/plain", "OK");
    }
  });

  // serve static files (css, photos, pdfs)
  server.serveStatic("/", SD, "/htmls/");
  server.serveStatic("/pdfs/", SD, "/pdfs/");
  server.serveStatic("/photos/", SD, "/photos/");

  server.begin();
}



void setup() {
  Serial.begin(115200);
  initSDCard();
  initAP();
}


void loop() {
  dnsServer.processNextRequest();
}