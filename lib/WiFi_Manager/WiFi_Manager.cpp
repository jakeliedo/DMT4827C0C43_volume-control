#include "WiFi_Manager.h"

// Constructor
WiFi_Manager::WiFi_Manager(WiFiNetwork* networks, int numNetworks, DMT_Display* display)
    : _networks(networks), _numNetworks(numNetworks), _display(display),
      _lastWiFiCheck(0), _lastRSSIUpdate(0), _autoReconnect(false) {
}

// Configuration
void WiFi_Manager::setDisplay(DMT_Display* display) {
    _display = display;
}

void WiFi_Manager::setAutoReconnect(bool enable, unsigned long checkInterval) {
    _autoReconnect = enable;
    _lastWiFiCheck = checkInterval;
}

void WiFi_Manager::setRSSIUpdateInterval(unsigned long interval) {
    _lastRSSIUpdate = interval;
}

// Connection management
bool WiFi_Manager::connectToWiFi() {
    Serial.println("🔄 Starting WiFi connection...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(1000);

    // Scan for available networks
    scanAndPrintNetworks();

    bool connected = false;
    for (int netIdx = 0; netIdx < _numNetworks; netIdx++) {
        const char* ssid = _networks[netIdx].ssid;
        const char* password = _networks[netIdx].password;

        showConnectionAttempt(ssid, password);

        Serial.print(">>> Attempting to connect to: ");
        Serial.println(ssid);

        WiFi.begin(ssid, password);

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 30) {
            delay(500);
            Serial.print(".");
            attempts++;
            if (attempts % 10 == 0) {
                Serial.printf(" [%d/30] Status: %d ", attempts, WiFi.status());
            }
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n✓ WiFi Connected Successfully!");
            Serial.print("  Network: ");
            Serial.println(ssid);
            Serial.print("  IP Address: ");
            Serial.println(WiFi.localIP());
            Serial.print("  Signal Strength: ");
            Serial.print(WiFi.RSSI());
            Serial.println(" dBm");
            Serial.print("  MAC Address: ");
            Serial.println(WiFi.macAddress());

            showConnectionSuccess(ssid, getCurrentRSSI());
            connected = true;
            break; // Stop after first successful connection
        } else {
            Serial.println("\n✗ WiFi Connection failed for SSID: " + String(ssid));
            Serial.printf("  Final WiFi Status: %d\n", WiFi.status());
            
            showConnectionFailure(ssid);
            WiFi.disconnect();
            delay(500);
        }
    }

    if (!connected) {
        Serial.println("\n✗ All WiFi connection attempts failed!");
        showAllConnectionsFailed();
    }

    return connected;
}

bool WiFi_Manager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

int WiFi_Manager::getCurrentRSSI() {
    if (isConnected()) {
        return WiFi.RSSI();
    }
    return 0;
}

String WiFi_Manager::getCurrentSSID() {
    if (isConnected()) {
        return WiFi.SSID();
    }
    return "";
}

String WiFi_Manager::getLocalIP() {
    if (isConnected()) {
        return WiFi.localIP().toString();
    }
    return "";
}

String WiFi_Manager::getMacAddress() {
    return WiFi.macAddress();
}

// Periodic tasks (call in main loop)
void WiFi_Manager::handleAutoReconnect() {
    if (!_autoReconnect) return;
    
    if (millis() - _lastWiFiCheck > 5000) { // Check every 5 seconds
        if (!isConnected()) {
            Serial.println("⚠️  WiFi disconnected, attempting to reconnect...");
            showDisconnected();
            connectToWiFi();
        } else {
            // Ensure WiFi icon is ON when connected
            if (_display) {
                _display->showWiFiIcon(true);
                _display->clearText(0x3400, 12); // Clear any failure message
            }
        }
        _lastWiFiCheck = millis();
    }
}

void WiFi_Manager::updateRSSIDisplay() {
    if (isConnected() && millis() - _lastRSSIUpdate > 2000) { // Update every 2 seconds
        if (_display) {
            _display->showRSSI(getCurrentRSSI(), 0x3400);
        }
        _lastRSSIUpdate = millis();
    }
}

// Network discovery
void WiFi_Manager::scanAndPrintNetworks() {
    int n = WiFi.scanNetworks();
    Serial.printf("Found %d WiFi networks:\n", n);
    for (int i = 0; i < n; ++i) {
        Serial.printf("  %d: %s (RSSI: %d dBm)%s\n", 
                      i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i), 
                      WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? " [OPEN]" : "");
    }
}

// Status display helpers
void WiFi_Manager::showConnectionAttempt(const char* ssid, const char* password) {
    if (_display) {
        // Clear VP 0x3200 with 40 spaces before showing new connection message
        _display->clearText(0x3200, 40);
        delay(50);
        
        String connectMsg = "Connecting to " + String(ssid) + " : " + String(password);
        _display->showConnectionStatus(connectMsg.c_str(), 0x3200);
        delay(100);
    }
}

void WiFi_Manager::showConnectionSuccess(const char* ssid, int rssi) {
    if (_display) {
        // Show success message with RSSI
        String wifiMsg = "Wifi Connected RSSI = " + String(rssi);
        _display->showConnectionStatus(wifiMsg.c_str(), 0x3300);
        delay(100);

        // Clear error message area
        _display->clearText(0x3400, 12);
        delay(100);

        // Turn on WiFi icon
        _display->showWiFiIcon(true);
        delay(100);
    }
}

void WiFi_Manager::showConnectionFailure(const char* ssid) {
    if (_display) {
        _display->showConnectionStatus("...", 0x3300);
        delay(100);
        _display->showConnectionError("Wifi failed", 0x3400);
        delay(100);
        _display->showWiFiIcon(false);
        delay(100);
    }
}

void WiFi_Manager::showAllConnectionsFailed() {
    if (_display) {
        _display->showConnectionStatus("All Wifi failed", 0x3300);
        delay(100);
        _display->showConnectionError("Wifi failed", 0x3400);
        delay(100);
        _display->showWiFiIcon(false);
        delay(100);
    }
}

void WiFi_Manager::showDisconnected() {
    if (_display) {
        _display->showWiFiIcon(false);
        delay(100);
        _display->showConnectionStatus("...", 0x3300);
        delay(100);
        _display->showConnectionError("Wifi failed", 0x3400);
        delay(100);
    }
}
