#ifndef MEZZO_CONTROLLER_H
#define MEZZO_CONTROLLER_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

struct ZoneInfo {
    uint16_t vpAddr;
    uint32_t zoneId;
    int zoneNumber;
    const char* name;
};

class Mezzo_Controller {
private:
    String _mezzoIP;
    int _mezzoPort;
    ZoneInfo* _zones;
    int _numZones;
    unsigned long _httpTimeout;
    
    // Callback for WiFi status check
    void (*_wifiFailureCallback)();
    
public:
    // Constructor
    Mezzo_Controller(const char* mezzoIP, int mezzoPort = 80);
    
    // Configuration
    void setZones(ZoneInfo* zones, int numZones);
    void setHTTPTimeout(unsigned long timeout);
    void setWiFiFailureCallback(void (*callback)());
    
    // Zone control
    bool sendVolumeToZone(uint16_t vpAddress, int volume);
    bool sendVolumeToZoneWithVPData(uint16_t vpAddress, uint16_t vpData);
    float readGainFromZone(uint16_t vpAddress);
    
    // Utility functions
    uint16_t mapGainToVP(float gain);
    float calculateGainFromVPData(uint16_t vpData);
    int findZoneIndex(uint16_t vpAddress);
    
    // API discovery
    void discoverEndpoints();
    
    // Volume mapping (moved from main)
    int mapVPToVolume(uint16_t vpData);
    
private:
    bool makeHTTPRequest(const String& url, const String& method, const String& payload = "");
    void checkWiFiAfterHTTPFailure();
};

#endif // MEZZO_CONTROLLER_H
