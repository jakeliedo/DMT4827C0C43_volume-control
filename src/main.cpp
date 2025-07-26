#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include "message.h" // Include message arrays for DMT display

// Include custom libraries
#include "DMT_Display.h"
#include "WiFi_Manager.h"
#include "Mezzo_Controller.h"

// Define pins for ESP32-C3 Super Mini
#define LED_PIN 8           // Built-in LED
#define UART_TX_PIN 21      // UART TX for DMT touchscreen
#define UART_RX_PIN 20      // UART RX for DMT touchscreen

// WiFi credentials in priority order
WiFiNetwork wifiNetworks[] = {
  {"Vinternal", "abcd123456"},
  {"Floor 9", "Veg@s123"},
  {"Roll", "0908800130"},
  {"MQTT", "@12345678"}
};
const int numWifiNetworks = sizeof(wifiNetworks) / sizeof(wifiNetworks[0]);

// Mezzo 604A device settings
const char* mezzoIP = "192.168.101.30";
const int mezzoPort = 80;

// Zone configuration for Mezzo
ZoneInfo zones[] = {
  {0x1100, 1868704443, 5, "Zone 1"},
  {0x1200, 4127125796, 6, "Zone 2"},
  {0x1300, 2170320302, 7, "Zone 3"},
  {0x1400, 2525320065, 8, "Zone 4"}
};
const int numZones = sizeof(zones) / sizeof(zones[0]);

// Create instances of our custom libraries
HardwareSerial DMTSerial(1);
DMT_Display dmtDisplay(&DMTSerial);
WiFi_Manager wifiManager(wifiNetworks, numWifiNetworks, &dmtDisplay);
Mezzo_Controller mezzoController(mezzoIP, mezzoPort);

// Global variables for volume change tracking
static unsigned long lastVolumeChangeTime = 0;
static uint16_t pendingVPAddress = 0;
static bool pendingGainRead = false;

// Callback function for VP data received from DMT
void onVPDataReceived(uint16_t vpAddress, uint16_t vpData) {
  uint8_t lowByte = vpData & 0xFF;
  Serial.printf("🔊 VP: 0x%04X = 0x%04X (Vol: %d)\n", vpAddress, vpData, lowByte);
  
  // Send volume to Mezzo controller
  mezzoController.sendVolumeToZoneWithVPData(vpAddress, vpData);
  
  // Schedule gain readback after 2 seconds
  lastVolumeChangeTime = millis();
  pendingVPAddress = vpAddress;
  pendingGainRead = true;
}

// Callback function for WiFi failure
void onWiFiFailure() {
  Serial.println("⚠️  WiFi disconnected detected after HTTP failure");
  dmtDisplay.showWiFiIcon(false);
  dmtDisplay.showConnectionStatus("...", 0x3300);
  dmtDisplay.showConnectionError("Wifi failed", 0x3400);
}

void setup() {
  // Initialize USB CDC Serial
  Serial.begin(115200);
  delay(2000); // Give time for Serial to initialize
  
  Serial.println("\n=== ESP32-C3 DMT Remote Controller ===");
  
  // Initialize LED pin
  pinMode(LED_PIN, OUTPUT);

  // Initialize DMT Display
  dmtDisplay.begin(115200, UART_RX_PIN, UART_TX_PIN);
  dmtDisplay.setVPDataCallback(onVPDataReceived);
  Serial.println("✓ DMT UART initialized (115200 baud, pins TX:" + String(UART_TX_PIN) + " RX:" + String(UART_RX_PIN) + ")");

  // Initialize Mezzo Controller
  mezzoController.setZones(zones, numZones);
  mezzoController.setWiFiFailureCallback(onWiFiFailure);

  // Initialize WiFi Manager
  wifiManager.setAutoReconnect(true, 5000);  // Auto reconnect every 5 seconds
  wifiManager.setRSSIUpdateInterval(2000);   // Update RSSI every 2 seconds

  Serial.println("✓ Hardware initialization complete");

  // Show booting message
  dmtDisplay.showBootMessage("Booting...");
  delay(100);

  // Start WiFi connection
  if (wifiManager.connectToWiFi()) {
    Serial.println("🔄 Initial volume update after WiFi connection...");
    // Update all zones with current gain values
    for (int i = 0; i < numZones; i++) {
      float currentGain = mezzoController.readGainFromZone(zones[i].vpAddr);
      if (currentGain > 0.0f) {
        uint16_t vpData = mezzoController.mapGainToVP(currentGain);
        dmtDisplay.writeVP(zones[i].vpAddr, vpData);
        delay(200);
      }
    }
  }

  Serial.println("=== System Ready ===\n");
}

void loop() {
  // Blink LED to show system is running
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 1000) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    lastBlink = millis();
  }
  
  // Handle WiFi auto-reconnect
  wifiManager.handleAutoReconnect();
  
  // Update RSSI display
  wifiManager.updateRSSIDisplay();
  
  // Handle incoming DMT data
  dmtDisplay.handleIncomingData();
  
  // Non-blocking gain readback after volume changes
  if (pendingGainRead && (millis() - lastVolumeChangeTime >= 2000)) {
    float actualGain = mezzoController.readGainFromZone(pendingVPAddress);
    if (actualGain > 0.0f) {
      uint16_t actualVPData = mezzoController.mapGainToVP(actualGain);
      dmtDisplay.writeVP(pendingVPAddress, actualVPData);
    }
    pendingGainRead = false;
  }
  
  // Show system heartbeat every 60 seconds
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 60000) {
    Serial.printf("💓 System Heartbeat - Uptime: %lu seconds, Free Heap: %d bytes\n", 
                  millis() / 1000, ESP.getFreeHeap());
    lastHeartbeat = millis();
  }
  
  // Periodically read current gain from Mezzo and update DMT display
  static unsigned long lastGainUpdate = 0;
  if (millis() - lastGainUpdate > 15000) { // Every 15 seconds
    if (wifiManager.isConnected()) {
      // Read and update all zones
      for (int i = 0; i < numZones; i++) {
        float currentGain = mezzoController.readGainFromZone(zones[i].vpAddr);
        if (currentGain > 0.0f) {
          uint16_t vpData = mezzoController.mapGainToVP(currentGain);
          dmtDisplay.writeVP(zones[i].vpAddr, vpData);
          delay(100);
        }
      }
    }
    lastGainUpdate = millis();
  }
  
  // Optional: Send command to read VP address 0x1000 every 60 seconds for testing
  static unsigned long lastVPRead = 0;
  if (millis() - lastVPRead > 60000) {
    dmtDisplay.readVP(0x1000);
    lastVPRead = millis();
  }
}
