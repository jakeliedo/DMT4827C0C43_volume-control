#include "Mezzo_Controller.h"
#include <cmath>

// Constructor
Mezzo_Controller::Mezzo_Controller(const char* mezzoIP, int mezzoPort)
    : _mezzoIP(mezzoIP), _mezzoPort(mezzoPort), _zones(nullptr), _numZones(0),
      _httpTimeout(2000), _wifiFailureCallback(nullptr) {
}

// Configuration
void Mezzo_Controller::setZones(ZoneInfo* zones, int numZones) {
    _zones = zones;
    _numZones = numZones;
}

void Mezzo_Controller::setHTTPTimeout(unsigned long timeout) {
    _httpTimeout = timeout;
}

void Mezzo_Controller::setWiFiFailureCallback(void (*callback)()) {
    _wifiFailureCallback = callback;
}

// Zone control
bool Mezzo_Controller::sendVolumeToZone(uint16_t vpAddress, int volume) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️  WiFi not connected, cannot send volume");
        return false;
    }
    
    int zoneIdx = findZoneIndex(vpAddress);
    if (zoneIdx == -1) return false;
    
    // Convert volume to gain using the mapping
    uint16_t vpData = map(volume, 0, 100, 0x100, 0x164); // VP_MIN_VALUE to VP_MAX_VALUE
    float gain = calculateGainFromVPData(vpData);
    
    HTTPClient http;
    String url = "http://" + _mezzoIP + "/iv/views/web/730665316/zone-controls/" + String(_zones[zoneIdx].zoneNumber);
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Installation-Client-Id", "0add066f-0458-4a61-9f57-c3a82fbb63f9");
    http.addHeader("Origin", "http://" + _mezzoIP);
    http.addHeader("Referer", "http://" + _mezzoIP + "/webapp/views/730665316");
    http.setTimeout(_httpTimeout);
    
    // Build JSON payload
    JsonDocument doc;
    JsonArray zonesArr = doc["Zones"].to<JsonArray>();
    JsonObject zoneObj = zonesArr.add<JsonObject>();
    zoneObj["Id"] = _zones[zoneIdx].zoneId;
    zoneObj["Gain"] = gain;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    unsigned long startTime = millis();
    int httpResponseCode = http.PUT(jsonString);
    unsigned long responseTime = millis() - startTime;
    
    bool success = false;
    if (httpResponseCode > 0) {
        Serial.printf("✅ HTTP %d (%.0f ms)\n", httpResponseCode, responseTime);
        success = true;
    } else {
        Serial.printf("❌ HTTP Error: %d\n", httpResponseCode);
        checkWiFiAfterHTTPFailure();
    }
    
    http.end();
    return success;
}

bool Mezzo_Controller::sendVolumeToZoneWithVPData(uint16_t vpAddress, uint16_t vpData) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    
    int zoneIdx = findZoneIndex(vpAddress);
    if (zoneIdx == -1) return false;
    
    // Extract low byte (dec_volume) from VP data
    uint8_t dec_volume = vpData & 0x00FF;
    
    // Convert dec_volume to gain using formula: GAIN = (2^(dec_volume/10))/1000
    float gain = calculateGainFromVPData(vpData);
    
    Serial.printf("🔊 Vol %d to %s (Gain: %.3f)\n", dec_volume, _zones[zoneIdx].name, gain);
    
    HTTPClient http;
    String url = "http://" + _mezzoIP + "/iv/views/web/730665316/zone-controls/" + String(_zones[zoneIdx].zoneNumber);
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Installation-Client-Id", "0add066f-0458-4a61-9f57-c3a82fbb63f9");
    http.addHeader("Origin", "http://" + _mezzoIP);
    http.addHeader("Referer", "http://" + _mezzoIP + "/webapp/views/730665316");
    http.setTimeout(300); // Very short timeout for volume changes
    
    // Build JSON payload
    JsonDocument doc;
    JsonArray zonesArr = doc["Zones"].to<JsonArray>();
    JsonObject zoneObj = zonesArr.add<JsonObject>();
    zoneObj["Id"] = _zones[zoneIdx].zoneId;
    zoneObj["Gain"] = gain;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpResponseCode = http.PUT(jsonString);
    bool success = false;
    if (httpResponseCode > 0) {
        Serial.printf("✅ HTTP %d\n", httpResponseCode);
        success = true;
    } else {
        Serial.printf("❌ HTTP Error: %d\n", httpResponseCode);
        checkWiFiAfterHTTPFailure();
    }
    
    http.end();
    return success;
}

float Mezzo_Controller::readGainFromZone(uint16_t vpAddress) {
    if (WiFi.status() != WL_CONNECTED) {
        return 0.0f;
    }
    
    int zoneIdx = findZoneIndex(vpAddress);
    if (zoneIdx == -1) return 0.0f;
    
    HTTPClient http;
    String url = "http://" + _mezzoIP + "/iv/views/web/730665316/zone-controls/" + String(_zones[zoneIdx].zoneNumber);
    
    http.begin(url);
    http.addHeader("Accept", "application/json, text/plain, */*");
    http.addHeader("Installation-Client-Id", "0add066f-0458-4a61-9f57-c3a82fbb63f9");
    http.addHeader("Origin", "http://" + _mezzoIP);
    http.addHeader("Referer", "http://" + _mezzoIP + "/webapp/views/730665316");
    http.setTimeout(_httpTimeout);
    
    int httpResponseCode = http.GET();
    float currentGain = 0.0f;
    
    if (httpResponseCode == 200) {
        String response = http.getString();
        
        // Parse JSON response to extract current gain
        JsonDocument respDoc;
        DeserializationError err = deserializeJson(respDoc, response);
        if (!err) {
            if (respDoc["Code"].is<int>() && respDoc["Code"].as<int>() == 0) {
                // Look for gain in Result.Gain.Value
                if (respDoc["Result"]["Gain"]["Value"].is<float>()) {
                    currentGain = respDoc["Result"]["Gain"]["Value"].as<float>();
                }
                // Alternative: look for gain in Result.Zones[0].Gain
                else if (respDoc["Result"]["Zones"].is<JsonArray>()) {
                    JsonArray resultZones = respDoc["Result"]["Zones"].as<JsonArray>();
                    if (resultZones.size() > 0 && resultZones[0]["Gain"].is<float>()) {
                        currentGain = resultZones[0]["Gain"].as<float>();
                    }
                }
            }
        }
    } else {
        Serial.printf("❌ HTTP Error: %d (readGainFromZone)\n", httpResponseCode);
        checkWiFiAfterHTTPFailure();
    }
    
    http.end();
    return currentGain;
}

// Utility functions
uint16_t Mezzo_Controller::mapGainToVP(float gain) {
    if (gain <= 0.0f) return 0x0000;  // Volume 0 → VP data 0x0000
    if (gain >= 1.0f) return 0x6400;  // Volume 100 → VP data 0x6400
    
    // Reverse calculation: find volume_converted from gain
    // GAIN = (2^(dec_volume/10))/1000, so dec_volume = 10 * log2(gain * 1000)
    float volume_converted = 10.0f * log2f(gain * 1000.0f);
    if (volume_converted < 0.0f) volume_converted = 0.0f;
    if (volume_converted > 100.0f) volume_converted = 100.0f;
    
    // Create VP data: high byte = volume_converted (0-100), low byte = 0x00
    uint8_t volumeByte = (uint8_t)round(volume_converted);
    uint16_t vpData = (volumeByte << 8) | 0x00;  // Format: 0xVV00 where VV is volume_converted
    
    return vpData;
}

float Mezzo_Controller::calculateGainFromVPData(uint16_t vpData) {
    // Extract low byte (dec_volume) from VP data
    uint8_t dec_volume = vpData & 0x00FF;
    
    // Convert dec_volume to gain using formula: GAIN = (2^(dec_volume/10))/1000
    float gain = 0.0f;
    if (dec_volume <= 0) {
        gain = 0.0f;
    } else if (dec_volume >= 100) {
        gain = 1.0f;  // Maximum gain
    } else {
        float exponent = (float)dec_volume / 10.0f;
        gain = pow(2.0f, exponent) / 1000.0f;
        if (gain > 1.0f) gain = 1.0f;  // Cap at 1.0
    }
    
    return gain;
}

int Mezzo_Controller::findZoneIndex(uint16_t vpAddress) {
    for (int i = 0; i < _numZones; i++) {
        if (_zones[i].vpAddr == vpAddress) {
            return i;
        }
    }
    return -1;
}

int Mezzo_Controller::mapVPToVolume(uint16_t vpData) {
    // VP data range: 0x100 (256) to 0x164 (356) = 100 steps
    const uint16_t VP_MIN_VALUE = 0x100;
    const uint16_t VP_MAX_VALUE = 0x164;
    
    if (vpData < VP_MIN_VALUE) vpData = VP_MIN_VALUE;
    if (vpData > VP_MAX_VALUE) vpData = VP_MAX_VALUE;
    
    // Map VP data (256-356) to volume (0-100)
    int volume = map(vpData, VP_MIN_VALUE, VP_MAX_VALUE, 0, 100);
    return volume;
}

// API discovery
void Mezzo_Controller::discoverEndpoints() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️  WiFi not connected, cannot discover endpoints");
        return;
    }
    
    Serial.println("🔍 Discovering Mezzo 604A API endpoints...");
    
    String testEndpoints[] = {
        "/iv/views/web/730665316", // Mezzo main endpoint
        "/iv/views/web/730665316/zone-controls/5",
        "/iv/views/web/730665316/zone-controls/6",
        "/iv/views/web/730665316/zone-controls/7",
        "/iv/views/web/730665316/zone-controls/8"
    };

    int numEndpoints = sizeof(testEndpoints) / sizeof(testEndpoints[0]);
    HTTPClient http;

    for (int i = 0; i < numEndpoints; i++) {
        String url = "http://" + _mezzoIP + testEndpoints[i];
        Serial.print("📡 Testing: ");
        Serial.println(url);

        http.begin(url);
        if (i >= 1) { // Zone control endpoints
            http.addHeader("Accept", "application/json, text/plain, */*");
            http.addHeader("Installation-Client-Id", "0add066f-0458-4a61-9f57-c3a82fbb63f9");
            http.addHeader("Origin", "http://" + _mezzoIP);
            http.addHeader("Referer", "http://" + _mezzoIP + "/webapp/views/730665316");
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
        delay(500);
    }

    Serial.println("🔍 Endpoint discovery complete\n");
}

// Private methods
bool Mezzo_Controller::makeHTTPRequest(const String& url, const String& method, const String& payload) {
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Installation-Client-Id", "0add066f-0458-4a61-9f57-c3a82fbb63f9");
    http.addHeader("Origin", "http://" + _mezzoIP);
    http.addHeader("Referer", "http://" + _mezzoIP + "/webapp/views/730665316");
    http.setTimeout(_httpTimeout);
    
    int httpResponseCode;
    if (method == "GET") {
        httpResponseCode = http.GET();
    } else if (method == "PUT") {
        httpResponseCode = http.PUT(payload);
    } else if (method == "POST") {
        httpResponseCode = http.POST(payload);
    } else {
        http.end();
        return false;
    }
    
    bool success = (httpResponseCode > 0);
    if (!success) {
        checkWiFiAfterHTTPFailure();
    }
    
    http.end();
    return success;
}

void Mezzo_Controller::checkWiFiAfterHTTPFailure() {
    if (_wifiFailureCallback && WiFi.status() != WL_CONNECTED) {
        _wifiFailureCallback();
    }
}
