#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "DMT_Display.h"

struct WiFiNetwork {
    const char* ssid;
    const char* password;
};

class WiFi_Manager {
private:
    WiFiNetwork* _networks;
    int _numNetworks;
    DMT_Display* _display;
    unsigned long _lastWiFiCheck;
    unsigned long _lastRSSIUpdate;
    bool _autoReconnect;
    
public:
    // Constructor
    WiFi_Manager(WiFiNetwork* networks, int numNetworks, DMT_Display* display = nullptr);
    
    // Configuration
    void setDisplay(DMT_Display* display);
    void setAutoReconnect(bool enable, unsigned long checkInterval = 5000);
    void setRSSIUpdateInterval(unsigned long interval = 10000);
    
    // Connection management
    bool connectToWiFi();
    bool isConnected();
    int getCurrentRSSI();
    String getCurrentSSID();
    String getLocalIP();
    String getMacAddress();
    
    // Periodic tasks (call in main loop)
    void handleAutoReconnect();
    void updateRSSIDisplay();
    
    // Network discovery
    void scanAndPrintNetworks();
    
    // Status display helpers
    void showConnectionAttempt(const char* ssid, const char* password);
    void showConnectionSuccess(const char* ssid, int rssi);
    void showConnectionFailure(const char* ssid);
    void showAllConnectionsFailed();
    void showDisconnected();
};

#endif // WIFI_MANAGER_H
