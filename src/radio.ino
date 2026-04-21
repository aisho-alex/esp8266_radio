/*
  ESP8266 FM Radio Controller
  Hardware: NodeMCU ESP8266 + RDA5807M FM Module
  Wiring:
    D1 (GPIO5) -> SDA
    D2 (GPIO4) -> SCL
    3.3V       -> VCC
    GND        -> GND
  
  Required libraries:
    - RDA5807 by Ricardo Lima Caratti (pu2clr)
    - ESP8266WiFi
    - ESP8266WebServer
    - ArduinoJson
    - EEPROM
*/

#include <Wire.h>
#include <RDA5807.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <LittleFS.h>

// ============ CONFIGURATION ============
// WiFi mode: true = AP mode, false = STA mode (connect to existing network)
#define WIFI_AP_MODE true

// STA mode credentials (ignored if AP mode)
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// AP mode settings
#define AP_SSID "FM-Radio"
#define AP_PASSWORD "12345678"  // min 8 chars, or empty for open network

// RDA5807M I2C pins
#define I2C_SDA D2  // GPIO5
#define I2C_SCL D1  // GPIO4

// EEPROM settings
#define EEPROM_SIZE 512
#define EEPROM_SETTINGS_ADDR 0
#define EEPROM_PRESETS_ADDR 64
#define MAX_PRESETS 20
#define PRESET_MAGIC 0x5244  // "RD" signature
#define SETTINGS_MAGIC 0x5353  // "SS" signature for settings

// ============ GLOBALS ============
RDA5807 rx;
ESP8266WebServer server(80);

struct Preset {
  char name[21];
  uint16_t frequency;  // in 100kHz units (e.g., 10150 = 101.5 MHz)
};

Preset presets[MAX_PRESETS];
int presetCount = 0;
bool isMuted = false;
int currentVolume = 1;
uint16_t currentFrequency = 8750;  // Default 87.5 MHz

// ============ EEPROM ============
void loadSettings() {
  uint16_t magic;
  EEPROM.get(EEPROM_SETTINGS_ADDR, magic);
  if (magic != SETTINGS_MAGIC) {
    // No valid settings found, use defaults
    currentFrequency = 8750;
    currentVolume = 1;
    return;
  }
  EEPROM.get(EEPROM_SETTINGS_ADDR + 2, currentFrequency);
  EEPROM.get(EEPROM_SETTINGS_ADDR + 4, currentVolume);
  
  // Validate loaded values
  if (currentFrequency < 8750 || currentFrequency > 10800) {
    currentFrequency = 8750;
  }
  if (currentVolume < 0 || currentVolume > 15) {
    currentVolume = 1;
  }
}

void saveSettings() {
  EEPROM.put(EEPROM_SETTINGS_ADDR, SETTINGS_MAGIC);
  EEPROM.put(EEPROM_SETTINGS_ADDR + 2, currentFrequency);
  EEPROM.put(EEPROM_SETTINGS_ADDR + 4, currentVolume);
  EEPROM.commit();
}

void loadPresets() {
  uint16_t magic;
  EEPROM.get(EEPROM_PRESETS_ADDR, magic);
  if (magic != PRESET_MAGIC) {
    presetCount = 0;
    return;
  }
  EEPROM.get(EEPROM_PRESETS_ADDR + 2, presetCount);
  if (presetCount < 0 || presetCount > MAX_PRESETS) {
    presetCount = 0;
    return;
  }
  for (int i = 0; i < presetCount; i++) {
    EEPROM.get(EEPROM_PRESETS_ADDR + 4 + i * sizeof(Preset), presets[i]);
  }
}

void savePresets() {
  EEPROM.put(EEPROM_PRESETS_ADDR, PRESET_MAGIC);
  EEPROM.put(EEPROM_PRESETS_ADDR + 2, presetCount);
  for (int i = 0; i < presetCount; i++) {
    EEPROM.put(EEPROM_PRESETS_ADDR + 4 + i * sizeof(Preset), presets[i]);
  }
  EEPROM.commit();
}

// ============ CORS ============
void setCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleOptions() {
  setCORS();
  server.send(200, "text/plain", "");
}

// ============ API HANDLERS ============
void handleStatus() {
  setCORS();
  StaticJsonDocument<256> doc;
  doc["frequency"] = rx.getFrequency();
  doc["volume"] = currentVolume;
  doc["muted"] = isMuted;
  doc["stereo"] = rx.isStereo();
  doc["rssi"] = rx.getRssi();
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleTune() {
  setCORS();
  StaticJsonDocument<128> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  
  uint16_t freq = doc["frequency"] | 0;
  if (freq >= 8750 && freq <= 10800) {
    currentFrequency = freq;
    rx.setFrequency(freq);
    delay(50);
    saveSettings();
  }
  
  StaticJsonDocument<128> resp;
  resp["frequency"] = rx.getFrequency();
  resp["rssi"] = rx.getRssi();
  resp["stereo"] = rx.isStereo();
  
  String response;
  serializeJson(resp, response);
  server.send(200, "application/json", response);
}

void handleSeek() {
  setCORS();
  StaticJsonDocument<128> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  
  String direction = "up";
  if (!error && doc["direction"]) {
    direction = doc["direction"].as<String>();
  }
  
  bool seekUp = (direction == "up");
  rx.setSeekThreshold(8);
  rx.seek(RDA_SEEK_WRAP, seekUp ? RDA_SEEK_UP : RDA_SEEK_DOWN);
  
  currentFrequency = rx.getFrequency();
  saveSettings();
  
  StaticJsonDocument<128> resp;
  resp["frequency"] = currentFrequency;
  resp["rssi"] = rx.getRssi();
  resp["stereo"] = rx.isStereo();
  
  String response;
  serializeJson(resp, response);
  server.send(200, "application/json", response);
}

void handleVolume() {
  setCORS();
  StaticJsonDocument<128> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  
  int vol = doc["volume"] | -1;
  if (vol >= 0 && vol <= 15) {
    currentVolume = vol;
    rx.setVolume(vol);
    saveSettings();
  }
  
  StaticJsonDocument<64> resp;
  resp["volume"] = currentVolume;
  
  String response;
  serializeJson(resp, response);
  server.send(200, "application/json", response);
}

void handleMute() {
  setCORS();
  StaticJsonDocument<128> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  
  bool mute = !isMuted;
  if (!error && doc.containsKey("muted")) {
    mute = doc["muted"];
  }
  
  isMuted = mute;
  rx.setMute(isMuted);
  
  StaticJsonDocument<64> resp;
  resp["muted"] = isMuted;
  
  String response;
  serializeJson(resp, response);
  server.send(200, "application/json", response);
}

void handleScan() {
  setCORS();
  StaticJsonDocument<1024> doc;
  JsonArray stations = doc.createNestedArray("stations");
  
  uint16_t originalFreq = rx.getFrequency();
  
  rx.setSeekThreshold(8);
  rx.setFrequency(8750);
  delay(100);
  
  for (int i = 0; i < 25; i++) {
    rx.seek(RDA_SEEK_WRAP, RDA_SEEK_UP);
    
    uint16_t freq = rx.getFrequency();
    uint8_t rssi = rx.getRssi();
    
    if (rssi >= 12) {
      JsonObject station = stations.createNestedObject();
      station["frequency"] = freq;
      station["rssi"] = rssi;
      station["stereo"] = rx.isStereo();
    }
    
    // Prevent infinite loop
    if (freq >= 10750) break;
    delay(100);
  }
  
  // Restore original frequency
  rx.setFrequency(originalFreq);
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleGetPresets() {
  setCORS();
  StaticJsonDocument<1024> doc;
  JsonArray arr = doc.createNestedArray("presets");
  
  for (int i = 0; i < presetCount; i++) {
    JsonObject p = arr.createNestedObject();
    p["name"] = presets[i].name;
    p["frequency"] = presets[i].frequency;
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleAddPreset() {
  setCORS();
  if (presetCount >= MAX_PRESETS) {
    server.send(400, "application/json", "{\"error\":\"Max presets reached\"}");
    return;
  }
  
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  
  const char* name = doc["name"] | "Station";
  uint16_t freq = doc["frequency"] | rx.getFrequency();
  
  strncpy(presets[presetCount].name, name, 20);
  presets[presetCount].name[20] = '\0';
  presets[presetCount].frequency = freq;
  presetCount++;
  savePresets();
  
  StaticJsonDocument<128> resp;
  resp["success"] = true;
  resp["index"] = presetCount - 1;
  
  String response;
  serializeJson(resp, response);
  server.send(200, "application/json", response);
}

void handleDeletePreset() {
  setCORS();
  String indexStr = server.pathArg(0);
  int index = indexStr.toInt();
  
  if (index < 0 || index >= presetCount) {
    server.send(400, "application/json", "{\"error\":\"Invalid index\"}");
    return;
  }
  
  for (int i = index; i < presetCount - 1; i++) {
    presets[i] = presets[i + 1];
  }
  presetCount--;
  savePresets();
  
  StaticJsonDocument<64> resp;
  resp["success"] = true;
  
  String response;
  serializeJson(resp, response);
  server.send(200, "application/json", response);
}

void handleNotFound() {
  if (server.method() == HTTP_OPTIONS) {
    handleOptions();
    return;
  }
  setCORS();
  server.send(404, "application/json", "{\"error\":\"Not found\"}");
}


void handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    server.send(500, "text/plain", "Failed to open index.html");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

// ============ SETUP & LOOP ============
void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n\n=== ESP8266 FM Radio ===");
  Serial.println(ESP.getFlashChipRealSize());
  
  // Init EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadSettings();
  loadPresets();
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
    return;
  }
  // Init I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // Init RDA5807
  rx.setup();
  rx.setVolume(currentVolume);
  rx.setFrequency(currentFrequency);  // Use saved frequency
  rx.setMono(false);       // Enable stereo
  rx.setBass(true);        // Enable bass boost
  rx.setMute(false);
  
  Serial.print("RDA5807M detected: ");
  Serial.println(rx.getDeviceId() != 0 ? "YES" : "NO");
  
  // WiFi setup
#if WIFI_AP_MODE
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println("AP Mode");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
#else
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
#endif
  
  // Web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/tune", HTTP_POST, handleTune);
  server.on("/api/seek", HTTP_POST, handleSeek);
  server.on("/api/volume", HTTP_POST, handleVolume);
  server.on("/api/mute", HTTP_POST, handleMute);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/presets", HTTP_GET, handleGetPresets);
  server.on("/api/presets", HTTP_POST, handleAddPreset);
  server.on("/api/presets/", HTTP_POST, handleAddPreset);
  server.on("/api/presets/{}", HTTP_DELETE, handleDeletePreset);
  
  server.onNotFound(handleNotFound);
  
  // CORS preflight
  server.on("/api/", HTTP_OPTIONS, handleOptions);
  
  server.begin();
  Serial.println("Web server started on port 80");
  Serial.println("============================");
}

void loop() {
  server.handleClient();
  delay(2);
}
