#include "esp_pm.h"
#include "esp_cpu.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <FS.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

DNSServer dnsServer;

// Network credentials and server setup
const char *ssid = "hyperlocalradio";
AsyncWebServer server(80);

// File paths
#define SD_CS 5
const char *WEB_DIR = "/web";
const char *SYSTEM_DIR = "/system";
const char *CONFIG_FILE = "/system/config.txt";
const char *MAIN_PAGE = "/web/index.html";
const char *PORTAL_PAGE = "/system/portal.html";

// Website content
String pageTitle = "hyperlocalradio";
String pageDescription = "Welcome to the hyper local radio station";

// Function to read and process HTML files (only basic variable replacement)
String loadAndProcessHTML(const char* filepath) {
  File file = SD.open(filepath);
  if (!file) {
    Serial.printf("Failed to open file: %s\n", filepath);
    return "";
  }
  
  String content = "";
  while (file.available()) {
    content += (char)file.read();
  }
  file.close();

  // Replace basic template variables only
  content.replace("{{PAGE_TITLE}}", pageTitle);
  content.replace("{{PAGE_DESCRIPTION}}", pageDescription);
  content.replace("{{SERVER_IP}}", WiFi.softAPIP().toString());
  
  return content;
}

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(240);

  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card failed!");
    return;
  }

  WiFi.softAP(ssid, NULL, 1, 0, 10);  
  dnsServer.start(53, "*", WiFi.softAPIP());
  
  if (MDNS.begin("hyperlocalradio")) {
    Serial.println("mDNS responder started");
  }

  loadConfig();
  setupServer();
  
  Serial.println("Access Point Started");
  Serial.println("IP Address: " + WiFi.softAPIP().toString());
}

void loadConfig() {
  File configFile = SD.open(CONFIG_FILE);
  if (configFile) {
    if (configFile.available()) {
      pageTitle = configFile.readStringUntil('\n');
      pageTitle.trim();
    }

    if (configFile.available()) {
      pageDescription = configFile.readStringUntil('\n');
      pageDescription.trim();
    }

    configFile.close();
    Serial.println("Config loaded:");
    Serial.println("Title: " + pageTitle);
    Serial.println("Description: " + pageDescription);
  } else {
    Serial.println("No config file found, using defaults");
  }
}

void setupServer() {
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  // Portal page handler - first page people see
  server.on("/portal", HTTP_GET, [](AsyncWebServerRequest *request) {
    String content = loadAndProcessHTML(PORTAL_PAGE);
    if (content.length() > 0) {
      request->send(200, "text/html", content);
    } else {
      request->send(500, "text/plain", "Portal page file not found");
    }
  });

  // Main page handler - serves your custom webpage directly from SD card
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Check if request is from captive portal (no proper host header)
    String host = request->host();
    if (host.indexOf("192.168.4.1") == -1 && host.indexOf("hyperlocalradio.local") == -1) {
      // Redirect captive portal to proper URL to open in native browser
      AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
      response->addHeader("Location", "http://hyperlocalradio.local/");
      request->send(response);
      return;
    }
    
    String content = loadAndProcessHTML(MAIN_PAGE);
    if (content.length() > 0) {
      request->send(200, "text/html", content);
    } else {
      request->send(500, "text/plain", "Main page file not found");
    }
  });

  // Generic file serving - serves any file from /web directory
  // Handles both /web/file.css and /file.css (automatically maps to /web/)
  server.onNotFound([](AsyncWebServerRequest *request) {
    String path = request->url();
    
    // Skip if this is a captive portal redirect (already handled below)
    if (request->host().indexOf("192.168.4.1") == -1 && 
        request->host().indexOf("hyperlocalradio.local") == -1 &&
        !request->host().equals("hyperlocalradio.local")) {
      AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
      response->addHeader("Location", "http://" + WiFi.softAPIP().toString() + "/portal");
      request->send(response);
      return;
    }
    
    // Try to serve file from /web directory
    // If URL is /style.css, try /web/style.css
    String webPath = path;
    if (!webPath.startsWith("/web/")) {
      webPath = "/web" + path;
    }
    
    if (SD.exists(webPath)) {
      String contentType = "text/plain";
      
      // Determine content type based on file extension
      if (webPath.endsWith(".html")) contentType = "text/html";
      else if (webPath.endsWith(".css")) contentType = "text/css";
      else if (webPath.endsWith(".js")) contentType = "application/javascript";
      else if (webPath.endsWith(".json")) contentType = "application/json";
      else if (webPath.endsWith(".png")) contentType = "image/png";
      else if (webPath.endsWith(".jpg") || webPath.endsWith(".jpeg")) contentType = "image/jpeg";
      else if (webPath.endsWith(".gif")) contentType = "image/gif";
      else if (webPath.endsWith(".svg")) contentType = "image/svg+xml";
      else if (webPath.endsWith(".mp3")) contentType = "audio/mpeg";
      else if (webPath.endsWith(".wav")) contentType = "audio/wav";
      else if (webPath.endsWith(".ogg")) contentType = "audio/ogg";
      else if (webPath.endsWith(".woff")) contentType = "font/woff";
      else if (webPath.endsWith(".woff2")) contentType = "font/woff2";
      else if (webPath.endsWith(".ttf")) contentType = "font/ttf";
      
      request->send(SD, webPath, contentType);
    } else {
      request->send(404, "text/plain", "File not found: " + path);
    }
  });

  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  delay(1);
}