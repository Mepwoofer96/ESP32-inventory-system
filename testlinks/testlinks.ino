//  Inventory.ino
//
//  Light it Up LLC
//
//  This is made for the inventory system for Light it Up LLC as a electronic inventory system that compiles and simplifies invenntory managment
//
//
//  Hardware used
//
//  NFC tags, ESP32 microcontroller, HW-125 SD Card Adapter
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

// possibly not needed
//#include <ESPmDNS.h> 

#include "FS.h"
#include "SD.h"
#include "SPI.h"

// for parsing user inputs 
#include "ArduinoJson.h"



const char* ssid = "Inventory_ESP";
const char* password = "Password?";
const char* fake_hostname = "inventory";

AsyncWebServer server(80);

//for dns lying
DNSServer dnsServer;

IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

// SD Card initalization
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

  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
}


//for other mode
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

void initAP(){

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(ssid, password);

  dnsServer.start(53, "*", apIP);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SD, "/htmls/index.html", "text/html");
  });
  server.serveStatic("/", SD, "/htmls/");
  server.begin();

}

void setup() {

  Serial.begin(115200);
  Serial.println("\n[*] Creating AP");
  
  //use sd
  initSDCard();

  //initalize ap
  initAP();
}

void loop() {
  dnsServer.processNextRequest();
}


// load table on index page
if(var == "PARTS_LIST"){

  File file = SPIFFS.open("/parts.csv","r")
  String output = "";

  while(file.availible()){
    String line = file.readStringUntil('\n');

    int commaIndex = line.indexOf(',');
    String name = line.substring(0, commaIndex);
    String count = line.substring(commaIndex+1);

            output += "<tr>";
        output += "<td><a href='/part?name=" + name + "'>" + name + "</a></td>";
        output += "<td>" + count + "</td>";
        output += "<td><a href='/pdfs/" + name + ".pdf'>View</a></td>";
        output += "</tr>";
  }
  file.close();
  return output;
  }

// FULL HANDLEING OF INPUTS TO JSON
// Route to handle adding a new part via admin page
server.on("/addpart", HTTP_POST, [](AsyncWebServerRequest *request){
    if(request->hasParam("name", true) && request->hasParam("count", true)){
        String name = request->getParam("name", true)->value();
        String count = request->getParam("count", true)->value();

        // Read existing JSON
        File file = SPIFFS.open("/jsons/parts.json", "r");
        DynamicJsonDocument doc(4096);
        deserializeJson(doc, file);
        file.close();

        // Add new part
        JsonArray arr = doc.as<JsonArray>();
        JsonObject newPart = arr.createNestedObject();
        newPart["name"] = name;
        newPart["count"] = count;

        // Write back
        File out = SPIFFS.open("/parts.json", "w");
        serializeJson(doc, out);
        out.close();

        request->send(200, "text/plain", "Part added");
    }
});



// Just for index page
String processor(const String& var){
    if(var == "PART_LIST"){
        File file = SPIFFS.open("/parts.json", "r");
        DynamicJsonDocument doc(4096);
        deserializeJson(doc, file);
        file.close();

        String output = "";
        JsonArray arr = doc.as<JsonArray>();
        for(JsonObject part : arr){
            output += "<li>" + String(part["name"].as<const char*>()) 
                    + " — " + String(part["count"].as<int>()) + "</li>";
        }
        return output;
    }
    return String();
}
