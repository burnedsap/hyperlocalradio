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
const char *MUSIC_DIR = "/music";
const char *WEB_DIR = "/web";
const char *CONFIG_FILE = "/config.txt";
const char *MAIN_PAGE = "/web/index.html";
const char *PORTAL_PAGE = "/web/portal.html";

// File management
File root;
String playlist[50];
int playlistSize = 0;

// Website content
String pageTitle = "hyperlocalradio";
String pageDescription = "Welcome to the hyper local radio station";

// Function to read and process HTML files
String loadAndInjectCode(const char* filepath) {
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

  // Replace basic template variables
  content.replace("{{PAGE_TITLE}}", pageTitle);
  content.replace("{{PAGE_DESCRIPTION}}", pageDescription);
  content.replace("{{SERVER_IP}}", WiFi.softAPIP().toString());

  // Inject player functionality if this is the main page
  if (String(filepath) == MAIN_PAGE) {
    // Find </body> tag
    int bodyEnd = content.indexOf("</body>");
    if (bodyEnd != -1) {
      // Inject player script before </body>
      String playerScript = R"(
        <script>
          let audioElement = new Audio();
          let currentIndex = 0;
          let isPlaying = false;
          let playlist = [];

          async function loadPlaylist() {
            const response = await fetch('/playlist');
            playlist = await response.json();
            const playlistDiv = document.getElementById('playlist');
            playlistDiv.innerHTML = playlist.map((song, index) => 
              `<div class='playlist-item' onclick='playSong(${index})'>${song.split('/').pop()}</div>`
            ).join('');
            if (playlist.length > 0) startPlaylist();
          }

          function startPlaylist() {
            audioElement.src = '/song?index=' + currentIndex;
            updateCurrentSong();
          }

          function updateCurrentSong() {
            document.getElementById('currentSong').textContent = 
              'Now playing: ' + playlist[currentIndex].split('/').pop();
          }

          function togglePlay() {
            if (isPlaying) {
              audioElement.pause();
              document.getElementById('playPauseBtn').textContent = 'Play';
              document.getElementById('status').textContent = 'Paused';
            } else {
              audioElement.play();
              document.getElementById('playPauseBtn').textContent = 'Pause';
              document.getElementById('status').textContent = 'Playing';
            }
            isPlaying = !isPlaying;
          }

          function playSong(index) {
            currentIndex = index;
            audioElement.src = '/song?index=' + currentIndex;
            audioElement.play();
            isPlaying = true;
            document.getElementById('playPauseBtn').textContent = 'Pause';
            updateCurrentSong();
          }

          audioElement.addEventListener('ended', () => {
            currentIndex = (currentIndex + 1) % playlist.length;
            audioElement.src = '/song?index=' + currentIndex;
            audioElement.play();
            updateCurrentSong();
          });

          loadPlaylist();
        </script>
      )";
      content = content.substring(0, bodyEnd) + playerScript + content.substring(bodyEnd);
    }
  }
  
  return content;
}

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(240);

  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card failed!");
    return;
  }

  // WiFi.softAP(ssid);
  WiFi.softAP(ssid, NULL, 1, 0, 10);  
  dnsServer.start(53, "*", WiFi.softAPIP());
  
  if (MDNS.begin("hyperlocalradio")) {
    Serial.println("mDNS responder started");
  }

  loadConfig();
  buildPlaylist();
  setupServer();
  
  Serial.println("Access Point Started");
  Serial.println("IP Address: " + WiFi.softAPIP().toString());
}
void buildPlaylist() {
  root = SD.open(MUSIC_DIR);
  if (!root) {
    Serial.println("Failed to open music directory");
    return;
  }

  File entry;
  playlistSize = 0;

  while ((entry = root.openNextFile()) && playlistSize < 50) {
    if (!entry.isDirectory() && entry.size() > 5000) {  // Check if file is not a directory and size > 5000 bytes
      playlist[playlistSize] = String(MUSIC_DIR) + "/" + String(entry.name());
      playlistSize++;
    }
    entry.close();
  }
  root.close();

  Serial.printf("Playlist built with %d songs (size > 5000 B)\n", playlistSize);
}
void loadConfig() {
  File configFile = SD.open(CONFIG_FILE);
  if (configFile) {
    // First line is title
    if (configFile.available()) {
      pageTitle = configFile.readStringUntil('\n');
      pageTitle.trim();
    }

    // Second line is description
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

  // Main page handler
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String content = loadAndInjectCode(MAIN_PAGE);
    if (content.length() > 0) {
      request->send(200, "text/html", content);
    } else {
      request->send(500, "text/plain", "Main page file not found");
    }
  });

  // Portal page handler
  server.on("/portal", HTTP_GET, [](AsyncWebServerRequest *request) {
    String content = loadAndInjectCode(PORTAL_PAGE);
    if (content.length() > 0) {
      request->send(200, "text/html", content);
    } else {
      request->send(500, "text/plain", "Portal page file not found");
    }
  });

  // Playlist handler
  server.on("/playlist", HTTP_GET, [](AsyncWebServerRequest *request) {
    String jsonPlaylist = "[";
    for (int i = 0; i < playlistSize; i++) {
      if (i > 0) jsonPlaylist += ",";
      jsonPlaylist += "\"" + playlist[i] + "\"";
    }
    jsonPlaylist += "]";
    request->send(200, "application/json", jsonPlaylist);
  });

  // Song streaming handler
  server.on("/song", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("index")) {
      request->send(400, "text/plain", "Missing index parameter");
      return;
    }

    int index = request->getParam("index")->value().toInt();
    if (index < 0 || index >= playlistSize) {
      request->send(400, "text/plain", "Invalid index");
      return;
    }

    AsyncWebServerResponse *response = request->beginResponse(SD, playlist[index], "audio/mpeg");
    response->addHeader("Content-Type", "audio/mpeg");
    request->send(response);
  });

  // Captive portal handlers
  server.onNotFound([](AsyncWebServerRequest *request) {
    if (request->host().equals("hyperlocalradio.local")) {
      request->send(404);
    } else {
      AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
      response->addHeader("Location", "http://" + WiFi.softAPIP().toString() + "/portal");
      request->send(response);
    }
  });

  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  delay(1);
}