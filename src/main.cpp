#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>

// Define pins for ESP32-C3 Super Mini
#define LED_PIN 8           // Built-in LED
#define UART_TX_PIN 21      // UART TX for DMT touchscreen
#define UART_RX_PIN 20      // UART RX for DMT touchscreen

// WiFi credentials in priority order
const char* wifiNetworks[][2] = {
  {"Floor 9", "Veg@s123"},
  {"Roll", "0908800130"},
  {"Vinternal", "abcd123456"}
};
const int numWifiNetworks = sizeof(wifiNetworks) / sizeof(wifiNetworks[0]);

// Mezzo 604A device settings
const char* mezzoIP = "192.168.101.30";
const int mezzoPort = 80;

// Volume mapping constants
#define VP_MIN_VALUE 0x100
#define VP_MAX_VALUE 0x164
#define VOLUME_MIN 0
#define VOLUME_MAX 100

// DMT Protocol Constants
#define DMT_HEADER_1 0x5A
#define DMT_HEADER_2 0xA5
#define DMT_CMD_READ_VP 0x83
#define DMT_CMD_READ_RTC 0x81
#define DMT_CMD_WRITE_VP 0x82
#define DMT_BUFFER_SIZE 64

// Create a second serial port for DMT touchscreen communication
HardwareSerial DMTSerial(1);

// Buffer for receiving DMT data
uint8_t dmtBuffer[DMT_BUFFER_SIZE];
int bufferIndex = 0;
bool frameStarted = false;

// Forward declarations
void connectToWiFi();
void sendVolumeToMezzo(int volume);
void sendVolumeToZone(uint16_t vpAddress, int volume);
void sendVolumeToZoneWithVPData(uint16_t vpAddress, uint16_t vpData);
void discoverMezzoEndpoints();
float readGainFromZone(uint16_t vpAddress);
void writeVPToDMT(uint16_t vpAddress, uint16_t vpData);
uint16_t mapGainToVP(float gain);
uint8_t calculateHighByteFromGain(float gain);
int mapVPToVolume(uint16_t vpData);
void processDMTFrame(uint8_t* frame, int frameLength);
void handleDMTData();

void setup() {
  // Initialize USB CDC Serial
  Serial.begin(115200);
  delay(2000); // Give time for Serial to initialize
  
  Serial.println("\n=== ESP32-C3 DMT Remote Controller ===");
  Serial.print("Chip Model: ");
  Serial.println(ESP.getChipModel());
  Serial.print("Chip Revision: ");
  Serial.println(ESP.getChipRevision());
  Serial.print("Flash Size: ");
  Serial.println(ESP.getFlashChipSize());
  Serial.print("Free Heap: ");
  Serial.println(ESP.getFreeHeap());
  
  // Initialize LED pin
  pinMode(LED_PIN, OUTPUT);
  Serial.println("✓ LED pin initialized");
  
  // Initialize UART for DMT touchscreen communication
  DMTSerial.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.println("✓ DMT UART initialized (115200 baud, pins TX:" + String(UART_TX_PIN) + " RX:" + String(UART_RX_PIN) + ")");
  
  Serial.println("✓ Hardware initialization complete");
  
  // Connect to WiFi
  connectToWiFi();
  
  // Discover available API endpoints for debugging
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("🔍 Running API endpoint discovery...");
    discoverMezzoEndpoints();
  }
  
  Serial.println("=== System Ready ===\n");
}

// Function to connect to WiFi networks in priority order
void connectToWiFi() {
  Serial.println("\n>>> Starting WiFi connection process...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(1000);
  
  Serial.println("Scanning for available WiFi networks...");
  int n = WiFi.scanNetworks();
  Serial.printf("Found %d networks:\n", n);
  for (int i = 0; i < n; ++i) {
    Serial.printf("  %d: %s (%d dBm) %s\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "Secured");
  }
  Serial.println();
  
  for (int i = 0; i < numWifiNetworks; i++) {
    Serial.print(">>> Attempting to connect to: ");
    Serial.print(wifiNetworks[i][0]);
    Serial.print(" (Priority ");
    Serial.print(i + 1);
    Serial.println(")");
    
    WiFi.begin(wifiNetworks[i][0], wifiNetworks[i][1]);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
      
      if (attempts % 10 == 0) {
        Serial.printf(" [%d/20] ", attempts);
      }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✓ WiFi Connected Successfully!");
      Serial.print("  Network: ");
      Serial.println(wifiNetworks[i][0]);
      Serial.print("  IP Address: ");
      Serial.println(WiFi.localIP());
      Serial.print("  Signal Strength: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
      Serial.print("  MAC Address: ");
      Serial.println(WiFi.macAddress());
      return;
    } else {
      Serial.println("\n✗ Connection failed");
      Serial.print("  WiFi Status: ");
      Serial.println(WiFi.status());
      WiFi.disconnect();
      delay(1000);
    }
  }
  
  Serial.println("✗ Failed to connect to any WiFi network!");
}

// Function to map gain (0.0-1.0) to VP data (reverse mapping using corrected formula)
uint16_t mapGainToVP(float gain) {
  if (gain <= 0.0f) return VP_MIN_VALUE;  // 0x100 = 256
  if (gain >= 1.0f) return VP_MAX_VALUE;  // 0x164 = 356
  
  // Reverse calculation from GAIN = (2^(vpData*10/356))/1000
  // So: gain * 1000 = 2^(vpData*10/356)
  // log2(gain * 1000) = vpData*10/356
  // vpData = (log2(gain * 1000) * 356) / 10
  float vpDataFloat = (log2f(gain * 1000.0f) * 356.0f) / 10.0f;
  
  uint16_t vpData = (uint16_t)round(vpDataFloat);
  if (vpData < VP_MIN_VALUE) vpData = VP_MIN_VALUE;
  if (vpData > VP_MAX_VALUE) vpData = VP_MAX_VALUE;
  
  // Debug log for gain to VP conversion
  Serial.printf("🔍 DEBUG: Gain to VP conversion: %.5f → VP: 0x%04X (%d)\n", 
                gain, vpData, vpData);
  Serial.printf("   Reverse formula: vpData = (log2(%.5f * 1000) * 356) / 10 = %.2f → %d\n", 
                gain, vpDataFloat, vpData);
  
  return vpData;
}

// Function to calculate high byte value from gain using reverse formula
uint8_t calculateHighByteFromGain(float gain) {
  if (gain <= 0.0f) return 0x00;
  if (gain >= 1.0f) return 0x64;  // 100 decimal = 0x64
  
  // Reverse formula: gain = (2^(volume/10))/1000
  // So: volume = 10 * log2(gain * 1000)
  float volume = 10.0f * log2f(gain * 1000.0f);
  if (volume < 0.0f) volume = 0.0f;
  if (volume > 100.0f) volume = 100.0f;
  
  uint8_t highByte = (uint8_t)round(volume);
  Serial.printf("🔢 Gain %.5f → Volume %.1f → High Byte 0x%02X (%d decimal)\n", 
                gain, volume, highByte, highByte);
  
  return highByte;
}

// Function to write data to DMT VP address
void writeVPToDMT(uint16_t vpAddress, uint16_t vpData) {
  Serial.printf("📝 Writing VP 0x%04X = 0x%04X (%d) to DMT\n", vpAddress, vpData, vpData);
  
  // DMT Write VP command: 5A A5 05 82 [VP_High] [VP_Low] [Data_High] [Data_Low]
  uint8_t writeVPCommand[] = {
    0x5A, 0xA5,                    // Header
    0x05,                          // Length
    0x82,                          // Write VP command
    (uint8_t)(vpAddress >> 8),     // VP address high byte
    (uint8_t)(vpAddress & 0xFF),   // VP address low byte
    (uint8_t)(vpData >> 8),        // Data high byte
    (uint8_t)(vpData & 0xFF)       // Data low byte
  };
  
  DMTSerial.write(writeVPCommand, sizeof(writeVPCommand));
  
  Serial.print("📤 DMT Write Command: ");
  for (int i = 0; i < sizeof(writeVPCommand); i++) {
    Serial.printf("%02X ", writeVPCommand[i]);
  }
  Serial.println();
}

// Function to read current gain from Mezzo API for specific zone
float readGainFromZone(uint16_t vpAddress) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️  WiFi not connected, cannot read gain");
        return 0.0f;
    }
    
    // Use same zone mapping as sendVolumeToZone
    struct ZoneInfo {
        uint16_t vpAddr;
        uint32_t zoneId;
        int zoneNumber;
        const char* name;
    };
    const ZoneInfo zones[] = {
        {0x1100, 1868704443, 5, "Zone 1"},
        {0x1200, 4127125796, 6, "Zone 2"},
        {0x1300, 2170320302, 7, "Zone 3"},
        {0x1400, 2525320065, 8, "Zone 4"}
    };
    const int numZones = sizeof(zones) / sizeof(zones[0]);
    
    int zoneIdx = -1;
    for (int i = 0; i < numZones; i++) {
        if (zones[i].vpAddr == vpAddress) {
            zoneIdx = i;
            break;
        }
    }
    
    if (zoneIdx == -1) {
        Serial.printf("❌ Unknown VP address for gain reading: 0x%04X\n", vpAddress);
        return 0.0f;
    }
    
    Serial.printf("📖 Reading current gain from %s (zoneNumber: %d)\n", zones[zoneIdx].name, zones[zoneIdx].zoneNumber);
    
    HTTPClient http;
    String url = String("http://") + mezzoIP + "/iv/views/web/730665316/zone-controls/" + String(zones[zoneIdx].zoneNumber);
    
    Serial.print("📡 GET Request to: ");
    Serial.println(url);
    
    http.begin(url);
    http.addHeader("Accept", "application/json, text/plain, */*");
    http.addHeader("Installation-Client-Id", "0add066f-0458-4a61-9f57-c3a82fbb63f9");
    http.addHeader("Origin", String("http://") + mezzoIP);
    http.addHeader("Referer", String("http://") + mezzoIP + "/webapp/views/730665316");
    http.setTimeout(5000);
    
    unsigned long startTime = millis();
    int httpResponseCode = http.GET();
    unsigned long responseTime = millis() - startTime;
    
    float currentGain = 0.0f;
    
    if (httpResponseCode == 200) {
        String response = http.getString();
        Serial.printf("✅ HTTP GET Response: %d (in %lu ms)\n", httpResponseCode, responseTime);
        Serial.print("📥 Response body: ");
        Serial.println(response);
        
        // Parse JSON response to extract current gain
        JsonDocument respDoc;
        DeserializationError err = deserializeJson(respDoc, response);
        if (!err) {
            Serial.println("🔍 DEBUG: Parsing gain from response:");
            
            if (respDoc["Code"].is<int>() && respDoc["Code"].as<int>() == 0) {
                // Look for gain in Result.Gain.Value
                if (respDoc["Result"]["Gain"]["Value"].is<float>()) {
                    currentGain = respDoc["Result"]["Gain"]["Value"].as<float>();
                    Serial.printf("   Found Result.Gain.Value: %.5f\n", currentGain);
                }
                // Alternative: look for gain in Result.Zones[0].Gain
                else if (respDoc["Result"]["Zones"].is<JsonArray>()) {
                    JsonArray resultZones = respDoc["Result"]["Zones"].as<JsonArray>();
                    if (resultZones.size() > 0 && resultZones[0]["Gain"].is<float>()) {
                        currentGain = resultZones[0]["Gain"].as<float>();
                        Serial.printf("   Found Result.Zones[0].Gain: %.5f\n", currentGain);
                    }
                }
            }
            
            if (currentGain == 0.0f) {
                Serial.println("⚠️  Could not extract gain value from response");
            }
        } else {
            Serial.println("🔍 DEBUG: Failed to parse JSON response for gain reading");
        }
    } else {
        Serial.printf("❌ HTTP GET Error: %d (after %lu ms)\n", httpResponseCode, responseTime);
    }
    
    http.end();
    return currentGain;
}

// Function to map VP data to volume percentage (using full 2-byte VP data from 0x100-0x164)
int mapVPToVolume(uint16_t vpData) {
  // VP data range: 0x100 (256) to 0x164 (356) = 100 steps
  if (vpData < VP_MIN_VALUE) vpData = VP_MIN_VALUE;  // 0x100 = 256
  if (vpData > VP_MAX_VALUE) vpData = VP_MAX_VALUE;  // 0x164 = 356
  
  // Map VP data (256-356) to volume (0-100)
  int volume = map(vpData, VP_MIN_VALUE, VP_MAX_VALUE, VOLUME_MIN, VOLUME_MAX);
  
  return volume;
}

// Function to send volume to Mezzo 604A
void sendVolumeToMezzo(int volume) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️  WiFi not connected, cannot send volume");
    return;
  }
  
  Serial.printf("🔊 Preparing to send volume %d%% to Mezzo 604A...\n", volume);
  
  HTTPClient http;
  String url = "http://" + String(mezzoIP) + ":" + String(mezzoPort) + "/api/volume";
  
  Serial.print("📡 Connecting to: ");
  Serial.println(url);
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000); // 5 second timeout
  
  // Create JSON payload
  JsonDocument doc;
  doc["volume"] = volume;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.print("📤 Sending JSON: ");
  Serial.println(jsonString);
  
  unsigned long startTime = millis();
  int httpResponseCode = http.POST(jsonString);
  unsigned long responseTime = millis() - startTime;
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("✅ HTTP Response: %d (in %lu ms)\n", httpResponseCode, responseTime);
    Serial.print("📥 Response body: ");
    Serial.println(response);
  } else {
    Serial.printf("❌ HTTP Error: %d (after %lu ms)\n", httpResponseCode, responseTime);
    Serial.println("   Possible causes: Network timeout, Mezzo device offline, wrong IP/port");
  }
  
  http.end();
  Serial.println("🔚 HTTP connection closed\n");
}

// Function to send volume to specific zone
void sendVolumeToZone(uint16_t vpAddress, int volume) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️  WiFi not connected, cannot send volume");
        return;
    }
    // Mapping VP address to ZoneId and zoneNumber
    struct ZoneInfo {
        uint16_t vpAddr;
        uint32_t zoneId;
        int zoneNumber;
        const char* name;
    };
    const ZoneInfo zones[] = {
        {0x1100, 1868704443, 5, "Zone 1"}, // Actual zone control index 5, corrected Zone ID from .har
        {0x1200, 4127125796, 6, "Zone 2"}, // Actual zone control index 6, corrected Zone ID from .har
        {0x1300, 2170320302, 7, "Zone 3"}, // Actual zone control index 7, corrected Zone ID from .har
        {0x1400, 2525320065, 8, "Zone 4"}  // Actual zone control index 8, Zone ID matches .har
    };
    const int numZones = sizeof(zones) / sizeof(zones[0]);
    int zoneIdx = -1;
    for (int i = 0; i < numZones; i++) {
        if (zones[i].vpAddr == vpAddress) {
            zoneIdx = i;
            break;
        }
    }
    if (zoneIdx == -1) {
        Serial.printf("❌ Unknown VP address: 0x%04X\n", vpAddress);
        Serial.println("🔍 DEBUG: Known VP addresses:");
        for (int i = 0; i < numZones; i++) {
            Serial.printf("   0x%04X → %s (ZoneId: %u)\n", zones[i].vpAddr, zones[i].name, zones[i].zoneId);
        }
        return;
    }
    // Convert VP data directly to gain using corrected formula
    // GAIN = (2^(vpData*10/356))/1000 where vpData is the raw VP value (256-356)
    // We need to get back the original vpData from the volume mapping
    uint16_t vpData = map(volume, VOLUME_MIN, VOLUME_MAX, VP_MIN_VALUE, VP_MAX_VALUE);
    
    float gain = 0.0f;
    if (vpData <= VP_MIN_VALUE) {
        gain = 0.0f;
    } else if (vpData >= VP_MAX_VALUE) {
        gain = 1.0f;  // Maximum gain
    } else {
        // Corrected exponential formula: GAIN = (2^(vpData*10/356))/1000
        float exponent = ((float)vpData * 10.0f) / 356.0f;
        gain = pow(2.0f, exponent) / 1000.0f;
        if (gain > 1.0f) gain = 1.0f;  // Cap at 1.0
    }
    
    // Debug: Show gain calculation details
    Serial.println("🔍 DEBUG: VP Data to Gain calculation:");
    Serial.printf("   Input Volume: %d%% (from VP data mapping)\n", volume);
    Serial.printf("   Reconstructed VP Data: 0x%04X (%d decimal)\n", vpData, vpData);
    Serial.printf("   VP Range: %d-%d (0x%03X-0x%03X)\n", VP_MIN_VALUE, VP_MAX_VALUE, VP_MIN_VALUE, VP_MAX_VALUE);
    Serial.printf("   VP Percentage: %.1f%% (%d/%d)\n", ((float)vpData / 356.0f) * 100.0f, vpData, 356);
    Serial.printf("   Formula: GAIN = (2^(%d*10/356))/1000 = (2^%.3f)/1000\n", vpData, ((float)vpData * 10.0f) / 356.0f);
    Serial.printf("   Power of 2: 2^%.3f = %.6f\n", ((float)vpData * 10.0f) / 356.0f, pow(2.0f, ((float)vpData * 10.0f) / 356.0f));
    Serial.printf("   Calculated GAIN: %.6f\n", gain);
    
    Serial.println("🔍 DEBUG: Zone and Gain calculation:");
    Serial.printf("   VP Address: 0x%04X → %s\n", vpAddress, zones[zoneIdx].name);
    Serial.printf("   Volume: %d%% → Gain: %.5f\n", volume, gain);
    Serial.printf("   Zone ID: %u, Zone Number: %d\n", zones[zoneIdx].zoneId, zones[zoneIdx].zoneNumber);
    
    Serial.printf("🔊 Sending volume %d%% (Gain: %.5f) to %s (ZoneId: %u, zoneNumber: %d)\n", volume, gain, zones[zoneIdx].name, zones[zoneIdx].zoneId, zones[zoneIdx].zoneNumber);
    HTTPClient http;
    
    // Primary URL works well based on endpoint discovery
    String url = String("http://") + mezzoIP + "/iv/views/web/730665316/zone-controls/" + String(zones[zoneIdx].zoneNumber);
    
    Serial.print("📡 PUT Request to: ");
    Serial.println(url);
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Installation-Client-Id", "0add066f-0458-4a61-9f57-c3a82fbb63f9");
    http.addHeader("Origin", String("http://") + mezzoIP);
    http.addHeader("Referer", String("http://") + mezzoIP + "/webapp/views/730665316");
    http.setTimeout(5000);
    
    // Build JSON payload
    JsonDocument doc;
    JsonArray zonesArr = doc["Zones"].to<JsonArray>();
    JsonObject zoneObj = zonesArr.add<JsonObject>();
    zoneObj["Id"] = zones[zoneIdx].zoneId;
    zoneObj["Gain"] = gain;
    String jsonString;
    serializeJson(doc, jsonString);
    
    Serial.print("📤 Sending JSON: ");
    Serial.println(jsonString);
    Serial.println("🔍 DEBUG: HTTP Headers:");
    Serial.println("   Content-Type: application/json");
    Serial.println("   Installation-Client-Id: 0add066f-0458-4a61-9f57-c3a82fbb63f9");
    Serial.printf("   Origin: http://%s\n", mezzoIP);
    Serial.printf("   Referer: http://%s/webapp/views/730665316\n", mezzoIP);
    
    unsigned long startTime = millis();
    int httpResponseCode = http.PUT(jsonString);
    unsigned long responseTime = millis() - startTime;
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.printf("✅ HTTP Response: %d (in %lu ms)\n", httpResponseCode, responseTime);
        Serial.print("📥 Response body: ");
        Serial.println(response);
        // Parse Powersoft API response for debugging
        if (response.length() > 0) {
            JsonDocument respDoc;
            DeserializationError err = deserializeJson(respDoc, response);
            if (!err) {
                Serial.println("🔍 DEBUG: Parsed response:");
                if (respDoc["Code"].is<int>()) {
                    int code = respDoc["Code"].as<int>();
                    Serial.printf("   Code: %d (%s)\n", code, 
                        code == 0 ? "OK" : 
                        code == 1 ? "DOWN" : 
                        code == 2 ? "DIFFERENT CONFIGURATION" : "UNKNOWN");
                }
                if (respDoc["Message"].is<const char*>()) {
                    Serial.printf("   Message: %s\n", respDoc["Message"].as<const char*>());
                }
            } else {
                Serial.println("🔍 DEBUG: Failed to parse JSON response");
            }
        }
    } else {
        Serial.printf("❌ HTTP Error: %d (after %lu ms)\n", httpResponseCode, responseTime);
        Serial.println("   Possible causes: Network timeout, Mezzo device offline, wrong IP/port");
    }
    http.end();
    Serial.println("🔚 HTTP connection closed\n");
}

// Function to send VP data directly to specific zone (corrected formula)
void sendVolumeToZoneWithVPData(uint16_t vpAddress, uint16_t vpData) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️  WiFi not connected, cannot send volume");
        return;
    }
    // Mapping VP address to ZoneId and zoneNumber
    struct ZoneInfo {
        uint16_t vpAddr;
        uint32_t zoneId;
        int zoneNumber;
        const char* name;
    };
    const ZoneInfo zones[] = {
        {0x1100, 1868704443, 5, "Zone 1"}, // Actual zone control index 5, corrected Zone ID from .har
        {0x1200, 4127125796, 6, "Zone 2"}, // Actual zone control index 6, corrected Zone ID from .har
        {0x1300, 2170320302, 7, "Zone 3"}, // Actual zone control index 7, corrected Zone ID from .har
        {0x1400, 2525320065, 8, "Zone 4"}  // Actual zone control index 8, Zone ID matches .har
    };
    const int numZones = sizeof(zones) / sizeof(zones[0]);
    int zoneIdx = -1;
    for (int i = 0; i < numZones; i++) {
        if (zones[i].vpAddr == vpAddress) {
            zoneIdx = i;
            break;
        }
    }
    if (zoneIdx == -1) {
        Serial.printf("❌ Unknown VP address: 0x%04X\n", vpAddress);
        Serial.println("🔍 DEBUG: Known VP addresses:");
        for (int i = 0; i < numZones; i++) {
            Serial.printf("   0x%04X → %s (ZoneId: %u)\n", zones[i].vpAddr, zones[i].name, zones[i].zoneId);
        }
        return;
    }
    
    // Convert VP data directly to gain using corrected formula
    // GAIN = (2^(vpData*10/356))/1000 where vpData is the raw VP value
    float gain = 0.0f;
    if (vpData <= VP_MIN_VALUE) {
        gain = 0.0f;
    } else if (vpData >= VP_MAX_VALUE) {
        gain = 1.0f;  // Maximum gain
    } else {
        // Corrected exponential formula: GAIN = (2^(vpData*10/356))/1000
        float exponent = ((float)vpData * 10.0f) / 356.0f;
        gain = pow(2.0f, exponent) / 1000.0f;
        if (gain > 1.0f) gain = 1.0f;  // Cap at 1.0
    }
    
    // Calculate percentage for display
    float percentage = ((float)vpData / 356.0f) * 100.0f;
    int displayVolume = mapVPToVolume(vpData);  // For display only
    
    // Debug: Show gain calculation details
    Serial.println("🔍 DEBUG: VP Data to Gain calculation (CORRECTED FORMULA):");
    Serial.printf("   Input VP Data: 0x%04X (%d decimal)\n", vpData, vpData);
    Serial.printf("   VP Percentage: %.3f%% (%d/356)\n", percentage, vpData);
    Serial.printf("   Display Volume: %d%% (mapped for display)\n", displayVolume);
    Serial.printf("   Formula: GAIN = (2^(%d*10/356))/1000 = (2^%.3f)/1000\n", vpData, ((float)vpData * 10.0f) / 356.0f);
    Serial.printf("   Power of 2: 2^%.3f = %.6f\n", ((float)vpData * 10.0f) / 356.0f, pow(2.0f, ((float)vpData * 10.0f) / 356.0f));
    Serial.printf("   Calculated GAIN: %.6f\n", gain);
    
    Serial.println("🔍 DEBUG: Zone mapping:");
    Serial.printf("   VP Address: 0x%04X → %s\n", vpAddress, zones[zoneIdx].name);
    Serial.printf("   VP Data: %d → Gain: %.6f\n", vpData, gain);
    Serial.printf("   Zone ID: %u, Zone Number: %d\n", zones[zoneIdx].zoneId, zones[zoneIdx].zoneNumber);
    
    Serial.printf("🔊 Sending VP data %d (%.3f%%) as Gain: %.6f to %s (ZoneId: %u, zoneNumber: %d)\n", 
                  vpData, percentage, gain, zones[zoneIdx].name, zones[zoneIdx].zoneId, zones[zoneIdx].zoneNumber);
    
    HTTPClient http;
    
    // Primary URL works well based on endpoint discovery
    String url = String("http://") + mezzoIP + "/iv/views/web/730665316/zone-controls/" + String(zones[zoneIdx].zoneNumber);
    
    Serial.print("📡 PUT Request to: ");
    Serial.println(url);
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Installation-Client-Id", "0add066f-0458-4a61-9f57-c3a82fbb63f9");
    http.addHeader("Origin", String("http://") + mezzoIP);
    http.addHeader("Referer", String("http://") + mezzoIP + "/webapp/views/730665316");
    http.setTimeout(5000);
    
    // Build JSON payload
    JsonDocument doc;
    JsonArray zonesArr = doc["Zones"].to<JsonArray>();
    JsonObject zoneObj = zonesArr.add<JsonObject>();
    zoneObj["Id"] = zones[zoneIdx].zoneId;
    zoneObj["Gain"] = gain;
    String jsonString;
    serializeJson(doc, jsonString);
    
    Serial.print("📤 Sending JSON: ");
    Serial.println(jsonString);
    
    unsigned long startTime = millis();
    int httpResponseCode = http.PUT(jsonString);
    unsigned long responseTime = millis() - startTime;
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.printf("✅ HTTP Response: %d (in %lu ms)\n", httpResponseCode, responseTime);
        Serial.print("📥 Response body: ");
        Serial.println(response);
        // Parse Powersoft API response for debugging
        if (response.length() > 0) {
            JsonDocument respDoc;
            DeserializationError err = deserializeJson(respDoc, response);
            if (!err) {
                Serial.println("🔍 DEBUG: Parsed response:");
                if (respDoc["Code"].is<int>()) {
                    int code = respDoc["Code"].as<int>();
                    Serial.printf("   Code: %d (%s)\n", code, 
                        code == 0 ? "OK" : 
                        code == 1 ? "DOWN" : 
                        code == 2 ? "DIFFERENT CONFIGURATION" : "UNKNOWN");
                }
                if (respDoc["Message"].is<const char*>()) {
                    Serial.printf("   Message: %s\n", respDoc["Message"].as<const char*>());
                }
            } else {
                Serial.println("🔍 DEBUG: Failed to parse JSON response");
            }
        }
    } else {
        Serial.printf("❌ HTTP Error: %d (after %lu ms)\n", httpResponseCode, responseTime);
    }
    http.end();
    Serial.println("🔚 HTTP connection closed\n");
}

// Function to discover available API endpoints on Mezzo device
void discoverMezzoEndpoints() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️  WiFi not connected, cannot discover endpoints");
        return;
    }
    
    Serial.println("🔍 Discovering Mezzo 604A API endpoints...");
    
    // Only test endpoints that actually work based on previous discovery
    String testEndpoints[] = {
        "/iv/views/web/730665316",       // Works - 200 OK (2959 bytes)
        "/webapp/views/730665316",       // Works - 200 OK (8145 bytes)
        "/",                             // Works - 200 OK (8145 bytes)
        "/iv/views/web/730665316/zone-controls/5",  // Test zone control endpoint
        "/iv/views/web/730665316/zone-controls/6",
        "/iv/views/web/730665316/zone-controls/7", 
        "/iv/views/web/730665316/zone-controls/8"
    };
    
    int numEndpoints = sizeof(testEndpoints) / sizeof(testEndpoints[0]);
    
    HTTPClient http;
    
    for (int i = 0; i < numEndpoints; i++) {
        String url = "http://" + String(mezzoIP) + testEndpoints[i];
        Serial.print("📡 Testing: ");
        Serial.println(url);
        
        http.begin(url);
        // Add headers for zone control endpoints
        if (i >= 3) { // Zone control endpoints
            http.addHeader("Accept", "application/json, text/plain, */*");
            http.addHeader("Installation-Client-Id", "0add066f-0458-4a61-9f57-c3a82fbb63f9");
            http.addHeader("Origin", String("http://") + mezzoIP);
            http.addHeader("Referer", String("http://") + mezzoIP + "/webapp/views/730665316");
        }
        http.setTimeout(3000);
        
        int httpResponseCode = http.GET();
        
        if (httpResponseCode > 0) {
            Serial.printf("✅ Response: %d - ", httpResponseCode);
            String contentType = http.header("Content-Type");
            int contentLength = http.getSize();
            Serial.printf("Content-Type: %s, Size: %d bytes\n", contentType.c_str(), contentLength);
            
            if (httpResponseCode == 200 && contentLength > 0 && contentLength < 1024) {
                String response = http.getString();
                Serial.println("📄 Response preview:");
                Serial.println(response.substring(0, 150) + (response.length() > 150 ? "..." : ""));
                Serial.println();
            }
        } else {
            Serial.printf("❌ Error: %d\n", httpResponseCode);
        }
        
        http.end();
        delay(500); // Small delay between requests
    }
    
    Serial.println("🔍 Endpoint discovery complete\n");
}

// Function to process complete DMT frame
void processDMTFrame(uint8_t* frame, int frameLength) {
  if (frameLength < 4) return; // Minimum frame size: header(2) + length(1) + command(1)
  
  uint8_t length = frame[2];
  uint8_t command = frame[3];
  
  Serial.print("📥 DMT Frame - Length: ");
  Serial.print(length);
  Serial.print(", Command: 0x");
  Serial.print(command, HEX);
  
  switch (command) {
    case DMT_CMD_READ_VP: // 0x83 - VP data
      if (frameLength >= 8) {
        uint16_t vpAddress = (frame[4] << 8) | frame[5];
        uint16_t vpData = (frame[6] << 8) | frame[7];
        Serial.print(", VP Address: 0x");
        Serial.print(vpAddress, HEX);
        Serial.print(", VP Data: 0x");
        Serial.print(vpData, HEX);
        Serial.print(" (");
        Serial.print(vpData);
        Serial.print(")");
        
        // Map VP data to volume for display and send raw VP data to Mezzo 604A
        int volume = mapVPToVolume(vpData);
        
        // Debug: Show VP data breakdown
        uint8_t highByte = (vpData >> 8) & 0xFF;
        uint8_t lowByte = vpData & 0xFF;
        float rawPercentage = ((float)vpData / 356.0f) * 100.0f;  // Use 356 as max for percentage
        
        Serial.printf("🔍 DEBUG: VP Data Analysis:\n");
        Serial.printf("   VP Raw: 0x%04X (%d decimal)\n", vpData, vpData);
        Serial.printf("   VP Range: 0x%03X-0x%03X (%d-%d)\n", VP_MIN_VALUE, VP_MAX_VALUE, VP_MIN_VALUE, VP_MAX_VALUE);
        Serial.printf("   VP High Byte = 0x%02X (%d), Low Byte = 0x%02X (%d)\n", highByte, highByte, lowByte, lowByte);
        Serial.printf("   Raw Percentage: %.3f%% (%d/356)\n", rawPercentage, vpData);
        Serial.printf("   Mapped Volume: %d%%\n", volume);
        
        // Send VP data directly to zone (not the mapped volume)
        sendVolumeToZoneWithVPData(vpAddress, vpData);
        
        // After sending volume, read back the actual gain and update DMT display
        delay(1000); // Wait for Mezzo to process the command
        float actualGain = readGainFromZone(vpAddress);
        if (actualGain > 0.0f) {
          uint16_t actualVPData = mapGainToVP(actualGain);
          Serial.printf("📊 Actual gain from Mezzo: %.5f → VP: 0x%04X\n", actualGain, actualVPData);
          writeVPToDMT(vpAddress, actualVPData);
        }
      }
      break;
    case DMT_CMD_READ_RTC: // 0x81 - RTC data
      Serial.print(", RTC Data: ");
      for (int i = 4; i < frameLength; i++) {
        Serial.print("0x");
        if (frame[i] < 0x10) Serial.print("0");
        Serial.print(frame[i], HEX);
        Serial.print(" ");
      }
      break;
      
    case DMT_CMD_WRITE_VP: // 0x82 - Write VP
      Serial.print(", Write VP command");
      break;
      
    default:
      Serial.print(", Unknown command");
      break;
  }
  
  Serial.println();
}

// Function to handle incoming UART data
void handleDMTData() {
  while (DMTSerial.available()) {
    uint8_t incomingByte = DMTSerial.read();
    
    // Look for frame start (0x5A 0xA5)
    if (!frameStarted) {
      if (bufferIndex == 0 && incomingByte == DMT_HEADER_1) {
        dmtBuffer[bufferIndex++] = incomingByte;
      } else if (bufferIndex == 1 && incomingByte == DMT_HEADER_2) {
        dmtBuffer[bufferIndex++] = incomingByte;
        frameStarted = true;
        Serial.print("🔍 DMT Frame header detected: ");
      } else {
        bufferIndex = 0; // Reset if header not found
      }
    } else {
      // Frame started, collect data
      if (bufferIndex < DMT_BUFFER_SIZE) {
        dmtBuffer[bufferIndex++] = incomingByte;
        
        // Check if we have received length byte (3rd byte)
        if (bufferIndex == 3) {
          // Length byte received, now we know the total frame size
          uint8_t frameLength = dmtBuffer[2] + 3; // Length + header(2) + length byte(1)
          Serial.print("Expected frame length: ");
          Serial.println(frameLength);
          
          if (frameLength > DMT_BUFFER_SIZE) {
            // Frame too long, reset
            bufferIndex = 0;
            frameStarted = false;
            Serial.println("⚠️  DMT Frame too long, resetting buffer");
            return;
          }
        }
        
        // Check if we have received the complete frame
        if (bufferIndex >= 3) {
          uint8_t expectedFrameLength = dmtBuffer[2] + 3;
          if (bufferIndex >= expectedFrameLength) {
            // Complete frame received, process it
            Serial.print("✓ Complete frame received (");
            Serial.print(bufferIndex);
            Serial.println(" bytes)");
            processDMTFrame(dmtBuffer, bufferIndex);
            
            // Reset for next frame
            bufferIndex = 0;
            frameStarted = false;
          }
        }
      } else {
        // Buffer overflow, reset
        bufferIndex = 0;
        frameStarted = false;
        Serial.println("⚠️  DMT Buffer overflow, resetting");
      }
    }
  }
}

void loop() {
  // Blink LED to show system is running
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 1000) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    lastBlink = millis();
  }
  
  // Check WiFi connection status every 30 seconds
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("⚠️  WiFi disconnected, attempting to reconnect...");
      connectToWiFi();
    } else {
      Serial.print("📶 WiFi Status: Connected to ");
      Serial.print(WiFi.SSID());
      Serial.print(" (");
      Serial.print(WiFi.RSSI());
      Serial.print(" dBm) IP: ");
      Serial.println(WiFi.localIP());
    }
    lastWiFiCheck = millis();
  }
  
  // Handle incoming DMT data
  handleDMTData();
  
  // Show system heartbeat every 60 seconds
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 60000) {
    Serial.printf("💓 System Heartbeat - Uptime: %lu seconds, Free Heap: %d bytes\n", 
                  millis() / 1000, ESP.getFreeHeap());
    lastHeartbeat = millis();
  }
  
  // Periodically read current gain from Mezzo and update DMT display every 10 seconds
  static unsigned long lastGainUpdate = 0;
  if (millis() - lastGainUpdate > 10000) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("🔄 Periodic gain update from Mezzo to DMT...");
      
      // Read and update all zones
      uint16_t vpAddresses[] = {0x1100, 0x1200, 0x1300, 0x1400};
      for (int i = 0; i < 4; i++) {
        float currentGain = readGainFromZone(vpAddresses[i]);
        if (currentGain > 0.0f) {
          uint16_t vpData = mapGainToVP(currentGain);
          writeVPToDMT(vpAddresses[i], vpData);
          delay(100); // Small delay between VP writes
        }
      }
    }
    lastGainUpdate = millis();
  }
  
  // Optional: Send command to read VP address 0x1000 every 30 seconds for testing
  static unsigned long lastVPRead = 0;
  if (millis() - lastVPRead > 30000) {
    // Send command to read VP: 5A A5 04 83 10 00
    uint8_t readVPCommand[] = {0x5A, 0xA5, 0x04, 0x83, 0x10, 0x00};
    DMTSerial.write(readVPCommand, sizeof(readVPCommand));
    Serial.println("📤 Sent VP read command for address 0x1000 (test)");
    lastVPRead = millis();
  }
}
