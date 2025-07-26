#ifndef DMT_DISPLAY_H
#define DMT_DISPLAY_H

#include <Arduino.h>
#include <HardwareSerial.h>

// DMT Protocol Constants
#define DMT_HEADER_1 0x5A
#define DMT_HEADER_2 0xA5
#define DMT_CMD_READ_VP 0x83
#define DMT_CMD_READ_RTC 0x81
#define DMT_CMD_WRITE_VP 0x82
#define DMT_CMD_WRITE_REG 0x80  // DGUS1 Write Register command
#define DMT_BUFFER_SIZE 64

// Volume mapping constants
#define VP_MIN_VALUE 0x100
#define VP_MAX_VALUE 0x164
#define VOLUME_MIN 0
#define VOLUME_MAX 100

class DMT_Display {
private:
    HardwareSerial* _serial;
    uint8_t _dmtBuffer[DMT_BUFFER_SIZE];
    int _bufferIndex;
    bool _frameStarted;
    
    // Callback function pointers
    void (*_vpDataCallback)(uint16_t vpAddress, uint16_t vpData);
    void (*_rtcDataCallback)(uint8_t* rtcData, int length);
    
public:
    // Constructor
    DMT_Display(HardwareSerial* serial);
    
    // Initialization
    void begin(unsigned long baudRate = 115200, int rxPin = 20, int txPin = 21);
    
    // Callback setters
    void setVPDataCallback(void (*callback)(uint16_t vpAddress, uint16_t vpData));
    void setRTCDataCallback(void (*callback)(uint8_t* rtcData, int length));
    
    // DGUS1 Register functions
    void writeRegister(uint8_t regAddress, uint8_t dataHigh, uint8_t dataLow);
    uint8_t readRegister(uint8_t regAddress);
    
    // DGUS1 VP functions - overloaded for different data types
    void writeVP(uint16_t vpAddress, int volume);           // Volume 0-100
    void writeVP(uint16_t vpAddress, uint16_t vpData);      // Raw VP data
    void writeText(uint16_t vpAddress, const char* text);   // ASCII text
    void writeChar(uint16_t vpAddress, char character);     // Single ASCII character
    uint16_t readVP(uint16_t vpAddress);
    
    // Volume mapping utilities
    uint16_t mapGainToVP(float gain);
    uint8_t calculateHighByteFromGain(float gain);
    int mapVPToVolume(uint16_t vpData);
    
    // Frame processing
    void handleIncomingData();
    void processDMTFrame(uint8_t* frame, int frameLength);
    
    // WiFi status display helpers
    void showWiFiIcon(bool isConnected);
    void showConnectionStatus(const char* message, uint16_t vpAddress = 0x3300);
    void showConnectionError(const char* message, uint16_t vpAddress = 0x3400);
    void clearText(uint16_t vpAddress, int numChars = 40);
    void showRSSI(int rssi, uint16_t vpAddress = 0x3400);
    
    // System status display
    void showBootMessage(const char* message = "Booting...");
    void showSystemReady();
};

#endif // DMT_DISPLAY_H
