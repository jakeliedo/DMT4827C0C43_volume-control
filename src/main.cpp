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
void discoverMezzoEndpoints();
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

// Function to map VP data to volume percentage
int mapVPToVolume(uint16_t vpData) {
  if (vpData < VP_MIN_VALUE) return VOLUME_MIN;
  if (vpData > VP_MAX_VALUE) return VOLUME_MAX;
  
  return map(vpData, VP_MIN_VALUE, VP_MAX_VALUE, VOLUME_MIN, VOLUME_MAX);
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
    // Convert volume (0-100) to gain using exponential mapping
    // Based on sample data: 80%→0.25119, 82%→0.2884, 84%→0.33113, 86%→0.38019
    float gain = 0.0f;
    if (volume <= 0) {
        gain = 0.0f;
    } else if (volume >= 100) {
        gain = 1.0f;  // Maximum gain
    } else {
        // Exponential mapping: gain = a * exp(b * volume) + c
        // Approximate formula based on data points
        gain = 0.01f * exp(0.045f * volume);
        if (gain > 1.0f) gain = 1.0f;  // Cap at 1.0
    }
    
    Serial.println("🔍 DEBUG: Zone and Gain calculation:");
    Serial.printf("   VP Address: 0x%04X → %s\n", vpAddress, zones[zoneIdx].name);
    Serial.printf("   Volume: %d%% → Gain: %.5f\n", volume, gain);
    Serial.printf("   Zone ID: %u, Zone Number: %d\n", zones[zoneIdx].zoneId, zones[zoneIdx].zoneNumber);
    
    Serial.printf("🔊 Sending volume %d%% (Gain: %.5f) to %s (ZoneId: %u, zoneNumber: %d)\n", volume, gain, zones[zoneIdx].name, zones[zoneIdx].zoneId, zones[zoneIdx].zoneNumber);
    HTTPClient http;
    
    // Chỉ sử dụng primary URL và Installation-Client-Id đúng
    String url1 = String("http://") + mezzoIP + "/iv/views/web/730665316/zone-controls/" + String(zones[zoneIdx].zoneNumber);
    String url2 = String("http://") + mezzoIP + "/api/zones/" + String(zones[zoneIdx].zoneId) + "/volume";
    String url3 = String("http://") + mezzoIP + "/zones/" + String(zones[zoneIdx].zoneNumber) + "/gain";
    
    Serial.print("📡 Primary URL: ");
    Serial.println(url1);
    Serial.print("📡 Alternative URL 1: ");
    Serial.println(url2);
    Serial.print("📡 Alternative URL 2: ");
    Serial.println(url3);
    
    http.begin(url1);
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
    
    // If first attempt fails with 404, try alternative endpoints
    if (httpResponseCode == 404) {
        Serial.println("🔄 Primary endpoint returned 404, trying alternative endpoints...");
        http.end();
        
        // Try alternative endpoint 1: Direct API
        Serial.print("📡 Trying alternative URL: ");
        Serial.println(url2);
        
        http.begin(url2);
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(5000);
        
        // Create simpler JSON payload for direct API
        JsonDocument doc2;
        doc2["volume"] = volume;
        doc2["gain"] = gain;
        
        String jsonString2;
        serializeJson(doc2, jsonString2);
        Serial.print("📤 Sending alternative JSON: ");
        Serial.println(jsonString2);
        
        httpResponseCode = http.POST(jsonString2);
        responseTime = millis() - startTime;
        
        if (httpResponseCode == 404) {
            Serial.println("🔄 Alternative endpoint 1 also returned 404, trying endpoint 2...");
            http.end();
            
            // Try alternative endpoint 2: Simple zones endpoint
            Serial.print("📡 Trying second alternative URL: ");
            Serial.println(url3);
            
            http.begin(url3);
            http.addHeader("Content-Type", "application/json");
            http.setTimeout(5000);
            
            // Create even simpler JSON payload
            JsonDocument doc3;
            doc3["gain"] = gain;
            
            String jsonString3;
            serializeJson(doc3, jsonString3);
            Serial.print("📤 Sending simple JSON: ");
            Serial.println(jsonString3);
            
            httpResponseCode = http.PUT(jsonString3);
            responseTime = millis() - startTime;
        }
    }
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

// Function to discover available API endpoints on Mezzo device
void discoverMezzoEndpoints() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️  WiFi not connected, cannot discover endpoints");
        return;
    }
    
    Serial.println("🔍 Discovering Mezzo 604A API endpoints...");
    
    // Common endpoints to try
    String testEndpoints[] = {
        "/api/",
        "/api/zones",
        "/api/info",
        "/api/status",
        "/zones",
        "/iv/views/web/730665316",
        "/webapp/views/730665316",
        "/help",
        "/",
        ""
    };
    
    int numEndpoints = sizeof(testEndpoints) / sizeof(testEndpoints[0]);
    
    HTTPClient http;
    
    for (int i = 0; i < numEndpoints; i++) {
        String url = "http://" + String(mezzoIP) + testEndpoints[i];
        Serial.print("📡 Testing: ");
        Serial.println(url);
        
        http.begin(url);
        http.setTimeout(3000);
        
        int httpResponseCode = http.GET();
        
        if (httpResponseCode > 0) {
            Serial.printf("✅ Response: %d - ", httpResponseCode);
            String contentType = http.header("Content-Type");
            int contentLength = http.getSize();
            Serial.printf("Content-Type: %s, Size: %d bytes\n", contentType.c_str(), contentLength);
            
            if (httpResponseCode == 200 && contentLength > 0 && contentLength < 2048) {
                String response = http.getString();
                Serial.println("📄 Response preview:");
                Serial.println(response.substring(0, 200) + (response.length() > 200 ? "..." : ""));
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
        
        // Map VP data to volume and send to Mezzo 604A
        int volume = mapVPToVolume(vpData);
        Serial.print(" ➜ Volume: ");
        Serial.print(volume);
        Serial.print("%");
        Serial.println();
        
        // Debug: Show mapping details
        Serial.println("🔍 DEBUG: VP to Volume mapping:");
        Serial.printf("   VP Raw: 0x%04X (%d decimal)\n", vpData, vpData);
        Serial.printf("   VP Range: 0x%03X-0x%03X (%d-%d)\n", VP_MIN_VALUE, VP_MAX_VALUE, VP_MIN_VALUE, VP_MAX_VALUE);
        Serial.printf("   Mapped Volume: %d%%\n", volume);
        
        // Send volume to specific zone
        sendVolumeToZone(vpAddress, volume);
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
