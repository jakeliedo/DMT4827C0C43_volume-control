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
        
        // Send volume to Mezzo 604A
        sendVolumeToMezzo(volume);
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
