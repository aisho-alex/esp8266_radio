/*
  ESP8266 FM Radio Controller
  Hardware: NodeMCU ESP8266 + RDA5807M FM Module
  Wiring:
    D1 (GPIO5) -> SDA (I2C)
    D2 (GPIO4) -> SCL (I2C)
    3.3V       -> VCC
    GND        -> GND
  
  Display Options (I2C shared bus):
    - LCD 1602 I2C (Blue): Default address 0x27
    - OLED 1.3" 128x64 I2C (SH1106): Default address 0x3C
  
  Required libraries:
    - RDA5807 by Ricardo Lima Caratti (pu2clr)
    - ESP8266WiFi
    - ESP8266WebServer
    - ArduinoJson
    - EEPROM
    - LiquidCrystal_I2C (for LCD 1602)
    - Adafruit SSD1306 (for OLED)
    - Adafruit GFX Library (for OLED)
*/

#include <Wire.h>
#include <RDA5807.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============ CONFIGURATION ============
// AP mode settings (fallback when STA connection fails)
#define AP_SSID "FM-Radio"
#define AP_PASSWORD "12345678"  // min 8 chars, or empty for open network
#define AP_CHANNEL 1

// WiFi connection settings
#define WIFI_CONNECT_TIMEOUT 10000  // 10 seconds
#define WIFI_CONNECT_RETRIES 3

// RDA5807M I2C pins
#define I2C_SDA D2  // GPIO5
#define I2C_SCL D1  // GPIO4

// Display type selection
// Set to 0: No display
// Set to 1: LCD 1602 I2C (Blue)
// Set to 2: OLED 1.3" 128x64 I2C (SH1106)
#define DISPLAY_TYPE 2

// LCD 1602 I2C configuration
#define LCD_ADDRESS 0x27
#define LCD_COLS 16
#define LCD_ROWS 2

// OLED 1.3" 128x64 I2C configuration
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Reset pin (-1 if sharing Arduino reset pin)

// EEPROM settings
#define EEPROM_SIZE 512
#define EEPROM_SETTINGS_ADDR 0
#define EEPROM_PRESETS_ADDR 64
#define EEPROM_WIFI_ADDR 256
#define MAX_PRESETS 20
#define PRESET_MAGIC 0x5244  // "RD" signature
#define SETTINGS_MAGIC 0x5353  // "SS" signature for settings
#define WIFI_MAGIC 0x5746  // "WF" signature for WiFi settings
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASSWORD_MAX_LEN 63

// ============ GLOBALS ============
RDA5807 rx;
ESP8266WebServer server(80);

// Display objects
#if DISPLAY_TYPE == 1
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);
#elif DISPLAY_TYPE == 2
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#endif

// Display update timer
unsigned long lastDisplayUpdate = 0;
const unsigned long displayUpdateInterval = 500;  // Update display every 500ms

struct Preset {
  char name[21];
  uint16_t frequency;  // in 100kHz units (e.g., 10150 = 101.5 MHz)
};

Preset presets[MAX_PRESETS];
int presetCount = 0;
bool isMuted = false;
int currentVolume = 1;
uint16_t currentFrequency = 8750;  // Default 87.5 MHz

// WiFi settings
char wifiSSID[WIFI_SSID_MAX_LEN + 1] = "";
char wifiPassword[WIFI_PASSWORD_MAX_LEN + 1] = "";
bool wifiConfigured = false;
bool wifiConnected = false;
unsigned long lastWiFiCheck = 0;
const unsigned long wifiCheckInterval = 30000;  // Check WiFi every 30 seconds

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

// ============ WIFI EEPROM ============
void loadWiFiSettings() {
  uint16_t magic;
  EEPROM.get(EEPROM_WIFI_ADDR, magic);
  if (magic != WIFI_MAGIC) {
    // No valid WiFi settings found
    wifiConfigured = false;
    wifiSSID[0] = '\0';
    wifiPassword[0] = '\0';
    return;
  }
  
  uint8_t ssidLen;
  EEPROM.get(EEPROM_WIFI_ADDR + 2, ssidLen);
  if (ssidLen > WIFI_SSID_MAX_LEN) ssidLen = WIFI_SSID_MAX_LEN;
  
  // Read SSID byte by byte
  for (int i = 0; i < ssidLen; i++) {
    EEPROM.get(EEPROM_WIFI_ADDR + 3 + i, wifiSSID[i]);
  }
  wifiSSID[ssidLen] = '\0';
  
  uint8_t passLen;
  EEPROM.get(EEPROM_WIFI_ADDR + 3 + WIFI_SSID_MAX_LEN + 1, passLen);
  if (passLen > WIFI_PASSWORD_MAX_LEN) passLen = WIFI_PASSWORD_MAX_LEN;
  
  // Read password byte by byte
  for (int i = 0; i < passLen; i++) {
    EEPROM.get(EEPROM_WIFI_ADDR + 4 + WIFI_SSID_MAX_LEN + 1 + i, wifiPassword[i]);
  }
  wifiPassword[passLen] = '\0';
  
  wifiConfigured = (ssidLen > 0);
}

void saveWiFiSettings(const char* ssid, const char* password) {
  uint8_t ssidLen = strlen(ssid);
  if (ssidLen > WIFI_SSID_MAX_LEN) ssidLen = WIFI_SSID_MAX_LEN;
  
  uint8_t passLen = strlen(password);
  if (passLen > WIFI_PASSWORD_MAX_LEN) passLen = WIFI_PASSWORD_MAX_LEN;
  
  EEPROM.put(EEPROM_WIFI_ADDR, WIFI_MAGIC);
  EEPROM.put(EEPROM_WIFI_ADDR + 2, ssidLen);
  
  // Write SSID byte by byte
  for (int i = 0; i < ssidLen; i++) {
    EEPROM.put(EEPROM_WIFI_ADDR + 3 + i, ssid[i]);
  }
  
  EEPROM.put(EEPROM_WIFI_ADDR + 3 + WIFI_SSID_MAX_LEN + 1, passLen);
  
  // Write password byte by byte
  for (int i = 0; i < passLen; i++) {
    EEPROM.put(EEPROM_WIFI_ADDR + 4 + WIFI_SSID_MAX_LEN + 1 + i, password[i]);
  }
  
  EEPROM.commit();
  
  // Update runtime variables
  strncpy(wifiSSID, ssid, WIFI_SSID_MAX_LEN);
  wifiSSID[WIFI_SSID_MAX_LEN] = '\0';
  strncpy(wifiPassword, password, WIFI_PASSWORD_MAX_LEN);
  wifiPassword[WIFI_PASSWORD_MAX_LEN] = '\0';
  wifiConfigured = true;
}

void clearWiFiSettings() {
  EEPROM.put(EEPROM_WIFI_ADDR, 0xFFFF);  // Invalid magic
  EEPROM.commit();
  wifiSSID[0] = '\0';
  wifiPassword[0] = '\0';
  wifiConfigured = false;
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

// ============ WIFI API HANDLERS ============
void handleWiFiStatus() {
  setCORS();
  StaticJsonDocument<256> doc;
  
  if (WiFi.getMode() == WIFI_AP) {
    doc["mode"] = "AP";
    doc["ap_ssid"] = AP_SSID;
    doc["ap_ip"] = WiFi.softAPIP().toString();
    doc["connected"] = false;
  } else {
    doc["mode"] = "STA";
    doc["sta_ssid"] = wifiSSID;
    doc["sta_ip"] = WiFi.localIP().toString();
    doc["connected"] = (WiFi.status() == WL_CONNECTED);
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleWiFiScan() {
  setCORS();
  StaticJsonDocument<1024> doc;
  JsonArray networks = doc.createNestedArray("networks");
  
  Serial.println("Scanning for networks...");
  int n = WiFi.scanNetworks();
  
  for (int i = 0; i < n; i++) {
    JsonObject net = networks.createNestedObject();
    net["ssid"] = WiFi.SSID(i);
    net["rssi"] = WiFi.RSSI(i);
    net["encryption"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
    net["channel"] = WiFi.channel(i);
  }
  
  WiFi.scanDelete();
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleWiFiSave() {
  setCORS();
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  
  const char* ssid = doc["ssid"];
  const char* password = doc["password"];
  
  if (!ssid || strlen(ssid) == 0) {
    server.send(400, "application/json", "{\"error\":\"SSID is required\"}");
    return;
  }
  
  // Save to EEPROM
  saveWiFiSettings(ssid, password);
  
  StaticJsonDocument<128> resp;
  resp["success"] = true;
  resp["message"] = "Settings saved. Reconnecting...";
  
  String response;
  serializeJson(resp, response);
  server.send(200, "application/json", response);
  
  // Schedule reconnect after response is sent
  delay(100);
  reconnectWiFi();
}

void handleWiFiConfig() {
  File file = LittleFS.open("/wifi.html", "r");
  if (!file) {
    server.send(500, "text/plain", "Failed to open wifi.html");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
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

// ============ DISPLAY FUNCTIONS ============
void initDisplay() {
#if DISPLAY_TYPE == 1
  // Initialize LCD 1602 I2C
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FM Radio");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(1000);
  Serial.println("LCD 1602 I2C initialized");
  
#elif DISPLAY_TYPE == 2
  // Initialize OLED 1.3" 128x64 I2C (SH1106)
  if(!display.begin(OLED_ADDRESS, false)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("FM Radio");
  display.println("Initializing...");
  display.display();
  delay(1000);
  Serial.println("OLED 1.3\" 128x64 I2C initialized");
  
#else
  Serial.println("No display configured");
#endif
}

void updateDisplay() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastDisplayUpdate < displayUpdateInterval) {
    return;
  }
  lastDisplayUpdate = currentMillis;
  
#if DISPLAY_TYPE == 1
  // Update LCD 1602
  lcd.clear();
  
  // Line 1: Frequency
  lcd.setCursor(0, 0);
  float freqMHz = currentFrequency / 100.0;
  lcd.print(freqMHz, 1);
  lcd.print(" MHz");
  
  // Line 2: Volume and status
  lcd.setCursor(0, 1);
  lcd.print("Vol:");
  lcd.print(currentVolume);
  if (isMuted) {
    lcd.print(" M");
  } else {
    lcd.print(" ");
  }
  if (rx.isStereo()) {
    lcd.print("ST");
  } else {
    lcd.print("MO");
  }
  
#elif DISPLAY_TYPE == 2
  // Update OLED 1.3" 128x64
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  
  // Frequency - large text at top
  display.setCursor(10, 5);
  float freqMHz = currentFrequency / 100.0;
  display.print(freqMHz, 1);
  display.println(" MHz");
  
  // Signal strength bar
  display.setTextSize(1);
  display.setCursor(0, 30);
  display.print("Signal: ");
  uint8_t rssi = rx.getRssi();
  display.print(rssi);
  display.print("/25");
  
  // Draw signal bar
  int barWidth = map(rssi, 0, 25, 0, 60);
  display.fillRect(60, 30, barWidth, 8, SSD1306_WHITE);
  
  // Volume and mute status
  display.setCursor(0, 42);
  display.print("Volume: ");
  display.print(currentVolume);
  display.print("/15");
  
  // Stereo/Mono indicator
  display.setCursor(0, 54);
  if (isMuted) {
    display.print("MUTED");
  } else {
    if (rx.isStereo()) {
      display.print("STEREO");
    } else {
      display.print("MONO");
    }
  }
  
  // WiFi status indicator (small)
  if (WiFi.status() == WL_CONNECTED) {
    display.fillCircle(120, 60, 3, SSD1306_WHITE);
  }
  
  display.display();
#endif
}

// ============ WIFI FUNCTIONS ============
bool connectToWiFi() {
  if (!wifiConfigured || strlen(wifiSSID) == 0) {
    return false;
  }
  
  Serial.println("Connecting to WiFi...");
  Serial.print("SSID: ");
  Serial.println(wifiSSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID, wifiPassword);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < WIFI_CONNECT_RETRIES) {
    delay(WIFI_CONNECT_TIMEOUT / WIFI_CONNECT_RETRIES);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Connected! IP: ");
    Serial.println(WiFi.localIP());
    wifiConnected = true;
    return true;
  } else {
    Serial.println();
    Serial.println("Failed to connect to WiFi");
    wifiConnected = false;
    return false;
  }
}

void startAPMode() {
  Serial.println("Starting AP mode...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);
  Serial.print("AP SSID: ");
  Serial.println(AP_SSID);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  wifiConnected = false;
}

void reconnectWiFi() {
  if (wifiConfigured && strlen(wifiSSID) > 0) {
    Serial.println("Attempting WiFi reconnection...");
    if (connectToWiFi()) {
      // Successfully connected in STA mode
    } else {
      // Fall back to AP mode
      startAPMode();
    }
  } else {
    // No WiFi config, start AP mode
    startAPMode();
  }
}

void checkWiFiConnection() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastWiFiCheck < wifiCheckInterval) {
    return;
  }
  lastWiFiCheck = currentMillis;
  
  if (WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost, attempting to reconnect...");
    reconnectWiFi();
  }
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
  
  // Init Display
  initDisplay();
  
  // Load WiFi settings from EEPROM
  loadWiFiSettings();
  
  // WiFi setup - try STA mode first, fall back to AP
  if (wifiConfigured && strlen(wifiSSID) > 0) {
    if (!connectToWiFi()) {
      startAPMode();
    }
  } else {
    startAPMode();
  }
  
  // Display IP address on startup
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected! IP: ");
    Serial.println(WiFi.localIP());
    
    // Show IP on display for a few seconds
    #if DISPLAY_TYPE == 2
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("WiFi Connected");
    display.println();
    display.println("IP Address:");
    display.setTextSize(2);
    display.println(WiFi.localIP().toString());
    display.display();
    delay(3000);
    #elif DISPLAY_TYPE == 1
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());
    delay(3000);
    #endif
  } else {
    Serial.print("AP Mode - IP: ");
    Serial.println(WiFi.softAPIP());
    
    // Show AP info on display
    #if DISPLAY_TYPE == 2
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("AP Mode");
    display.println();
    display.println("SSID:");
    display.setTextSize(2);
    display.println(AP_SSID);
    display.setTextSize(1);
    display.println();
    display.print("IP: ");
    display.println(WiFi.softAPIP().toString());
    display.display();
    delay(3000);
    #elif DISPLAY_TYPE == 1
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("AP Mode:");
    lcd.print(AP_SSID);
    lcd.setCursor(0, 1);
    lcd.print(WiFi.softAPIP().toString());
    delay(3000);
    #endif
  }
  
  // Web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi", HTTP_GET, handleWiFiConfig);
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
  
  // WiFi API routes
  server.on("/api/wifi/status", HTTP_GET, handleWiFiStatus);
  server.on("/api/wifi/scan", HTTP_GET, handleWiFiScan);
  server.on("/api/wifi/save", HTTP_POST, handleWiFiSave);
  
  server.onNotFound(handleNotFound);
  
  // CORS preflight
  server.on("/api/", HTTP_OPTIONS, handleOptions);
  
  server.begin();
  Serial.println("Web server started on port 80");
  Serial.println("============================");
}

void loop() {
  server.handleClient();
  updateDisplay();
  checkWiFiConnection();
  delay(2);
}
