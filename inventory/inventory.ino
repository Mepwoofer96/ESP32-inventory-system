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
//  Over the air updates, infinte scalability, PDF export, Automatic Renewal (when stock is low)
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


const char* apssid = "Inventory_ESP";
const char* appassword = "Password?";
const char* fake_hostname = "inventory";

bool wifiRequested = false;
bool apRequested = false;
bool serverStarted = false;

String wifiSSID = "";
String wifiPass = "";

AsyncWebServer* server = nullptr;

//for dns lying
DNSServer dnsServer;

IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

// For getting the correcct part when asked on html
String currentPartName = "";
String cachedName = "";
String cachedCount = "";
String cssCache = "";


String escapePDFText(String s) {
  s.replace("\\", "\\\\");
  s.replace("(", "\\(");
  s.replace(")", "\\)");
  return s;
}

void loadFileCache() {
  File f = SD.open("/htmls/style.css", "r");
  if (f) {
    while (f.available()) {
      cssCache += (char)f.read();
    }
    f.close();
    Serial.println("CSS cached");
  }
}

void loadPartCache(const String& name) {
  if (cachedName == name) return;
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
}

// PROCESSOR
// handles all %VARS% in htmls
String processor(const String& var) {
  // If var is %PART_LIST% used in index to show all current inventory options
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
      String name = line.subs… {
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
      return String();
    }
  }
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

const char* adminUser = "admin";
const char* adminPass = "password?";


// Start pages including pages that send data
void initRoutes() {

  server = new AsyncWebServer(80);
  // index page
  server->on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SD, "/htmls/index.html", "text/html", false, processor);
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
    if (!request->authenticate(adminUser, adminPass)) {
      return request->requestAuthentication();
    }
    request->send(SD, "/htmls/admin.html", "text/html", false, processor);
  });

  // add part — appends to CSV and saves uploaded files
  server->on("/addpart", HTTP_POST, [](AsyncWebServerRequest* request) {
      if (!request->authenticate(adminUser, adminPass)) {
        return request->requestAuthentication();
      }
      if (request->hasParam("name", true) && request->hasParam("count", true)) {
        String name = request->getParam("name", true)->value();
        String count = request->getParam("count", true)->value();

        File csv = SD.open("/data/parts.csv", FILE_APPEND);
        if (csv) {
          csv.println(name + "," + count);
          csv.close();
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

  // write nfc tag
  server->on("/writetag", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->authenticate(adminUser, adminPass)) {
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

  server->on("/updatecount", HTTP_POST, [](AsyncWebServerRequest* request) {
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

  // server->on("/internalnfc"){} //SHOULD CONNECT TO INTERNAL NFC WRITER TO WRITE A TAG

  //generates a PDF report of the parts.csv inventory
  server->on("/printpdf", HTTP_GET, [](AsyncWebServerRequest* request) {
    File csv = SD.open("/data/parts.csv", "r");
    if (!csv) {
      request->send(404, "text/plain", "No inventory data found");
      return;
    }

    String content = "";
    int y = 730;
    int lineHeight = 18;

    // Title at the top
    content += "BT /F1 20 Tf 50 770 Td (Part Number) Tj ET\n";
    content += "BT /F1 10 Tf 50 750 Td (Count) Tj ET\n";  // simple column label

    while (csv.available()) {
      String line = csv.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;

      int commaIndex = line.indexOf(',');
      String name = line.substring(0, commaIndex);
      String count = line.substring(commaIndex + 1);

      String row = escapePDFText(name + "     " + count);
      content += "BT /F1 12 Tf 50 " + String(y) + " Td (" + row + ") Tj ET\n";
      y -= lineHeight;

      // basic overflow guard — stop before running off the page
      if (y < 50) break;
    }
    csv.close();

    // --- assemble the PDF ---
    String pdf = "%PDF-1.4\n";
    pdf += "1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj\n";
    pdf += "2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj\n";
    pdf += "3 0 obj<</Type/Page/Parent 2 0 R/Resources<</Font<</F1 5 0 R>>>>/MediaBox[0 0 612 792]/Contents 4 0 R>>endobj\n";
    pdf += "4 0 obj<</Length " + String(content.length()) + ">>stream\n";
    pdf += content;
    pdf += "endstream endobj\n";
    pdf += "5 0 obj<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>endobj\n";
    int xrefStart = pdf.length();
    pdf += "xref\n0 6\n0000000000 65535 f \n";
    pdf += "trailer<</Size 6/Root 1 0 R>>\n";
    pdf += "startxref\n" + String(xrefStart) + "\n%%EOF";

    AsyncWebServerResponse* response = request->beginResponse(200, "application/pdf", pdf);
    response->addHeader("Content-Disposition", "attachment; filename=inventory.pdf");
    request->send(response);
  });

  // serve static files (css, photos, pdfs)
  server->serveStatic("/pdfs/", SD, "/pdfs/");
  server->serveStatic("/photos/", SD, "/photos/");
}

//For AP mode
void initAP() {
  MDNS.end();
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(apssid, appassword);
  dnsServer.start(53, "inventory.io", apIP);
  if (!serverStarted) {
    server->begin();
    serverStarted = true;
    Serial.println("Server started");
  }
}

// For Wifi mode
void initWiFi() {
  WiFi.softAPdisconnect(true);
  dnsServer.stop();
  WiFi.mode(WIFI_STA);
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

  //use sd
  initSDCard();

  delay(500);

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
}