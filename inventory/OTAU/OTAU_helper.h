// OTAU_helper.h
//
// Reusable OTA update helper for ESP32 projects.
// Handles: firmware version check, firmware.bin flashing via Update.h,
// and generic static file (HTML/CSS/etc) syncing from a URL to SD/SPIFFS.
//
// USAGE (in your main .ino):
//
//   #include "OTAU_helper.h"
//
//   void setup() {
//     ...
//     OTAUInit(
//       "https://raw.githubusercontent.com/you/repo/main/version.txt",
//       "https://raw.githubusercontent.com/you/repo/main/firmware.bin",
//       "1.0.0"   // <-- bump this every release, must match version.txt
//     );
//   }
//
//   // Wire this into your own route, e.g.:
//   server->on("/checkupdate", HTTP_GET, [](AsyncWebServerRequest* request) {
//     if (WiFi.status() != WL_CONNECTED) {
//       request->send(200, "text/plain", "offline");
//       return;
//     }
//     request->send(200, "text/plain", "Checking for updates...");
//     OTAU();  // reboots automatically if it flashes new firmware
//   });
//
//   // Optional: sync static files after confirming firmware is current
//   OTAUDownloadFileToSD("https://raw.githubusercontent.com/you/repo/main/htmls/index.html", "/htmls/index.html");
//
// NOTES:
// - Requires SD.h (or swap for SPIFFS.h) already included in your main sketch.
// - Uses client.setInsecure() -- skips TLS cert validation. Fine for personal/
//   hobby projects on a private network; not suitable if you need to guard
//   against a MITM on the update channel.
// - Only call OTAUCheckForUpdate() while in STA mode with a live connection --
//   it will simply fail harmlessly (no throw) if there's no internet, but
//   it's better to gate the route on WiFi.status() == WL_CONNECTED first.
// - There is no rollback. A bad firmware.bin can require a USB reflash to
//   recover. Test OTAU on a device you can physically reach before relying
//   on it for deployed units.

#ifndef OTAU_HELPER_H
#define OTAU_HELPER_H

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>

static String _OTAU_versionURL = "";
static String _OTAU_firmwareURL = "";
static String _OTAU_currentVersion = "";

// Call once in setup() before using the other functions.
inline void OTAUInit(const String& versionURL, const String& firmwareURL, const String& currentVersion) {
  _OTAU_versionURL = versionURL;
  _OTAU_firmwareURL = firmwareURL;
  _OTAU_currentVersion = currentVersion;
}

// Downloads any file from a URL and writes it to the given path on SD.
// Works for HTML, CSS, CSVs, images -- anything that isn't firmware.
// Returns true on success.
inline bool OTAUDownloadFileToSD(const String& url, const String& sdPath) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, url);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.println("OTAU asset download failed (" + String(httpCode) + "): " + url);
    http.end();
    return false;
  }

  File file = SD.open(sdPath, FILE_WRITE);
  if (!file) {
    Serial.println("OTAU asset: could not open " + sdPath + " for writing");
    http.end();
    return false;
  }
  http.writeToStream(&file);
  file.close();
  http.end();
  Serial.println("OTAU asset updated: " + sdPath);
  return true;
}

// Flashes new firmware from _OTAU_firmwareURL. Reboots automatically on success.
// Not meant to be called directly -- use OTAUCheckForUpdate() instead, which
// only calls this after confirming the remote version actually differs.
inline void OTAUPerformFirmwareUpdate() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, _OTAU_firmwareURL);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    if (contentLength > 0 && Update.begin(contentLength)) {
      WiFiClient* stream = http.getStreamPtr();
      size_t written = Update.writeStream(*stream);
      if (written == (size_t)contentLength && Update.end()) {
        Serial.println("OTAU firmware update successful, rebooting...");
        http.end();
        ESP.restart();
      } else {
        Serial.println("OTAU firmware update failed: " + String(Update.getError()));
      }
    } else {
      Serial.println("OTAU: not enough space or bad content length");
    }
  } else {
    Serial.println("OTAU firmware fetch failed (" + String(httpCode) + ")");
  }
  http.end();
}

// Fetches the remote version string and flashes new firmware only if it
// differs from the version passed into OTAUInit(). Safe to call repeatedly --
// it's a no-op if already up to date.
inline void OTAUCheckForUpdate() {
  if (_OTAU_versionURL.length() == 0) {
    Serial.println("OTAU: OTAUInit() was never called");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, _OTAU_versionURL);
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String remoteVersion = http.getString();
    remoteVersion.trim();
    Serial.println("OTAU: local=" + _OTAU_currentVersion + " remote=" + remoteVersion);
    if (remoteVersion != _OTAU_currentVersion) {
      Serial.println("OTAU: new version available, flashing...");
      http.end();
      OTAUPerformFirmwareUpdate();
      return;
    } else {
      Serial.println("OTAU: already up to date");
    }
  } else {
    Serial.println("OTAU: version check failed (" + String(code) + ")");
  }
  http.end();
}

#endif // OTAU_HELPER_H