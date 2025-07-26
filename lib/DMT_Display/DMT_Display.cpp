#include "DMT_Display.h"
#include <cmath>

// Constructor
DMT_Display::DMT_Display(HardwareSerial* serial) 
    : _serial(serial), _bufferIndex(0), _frameStarted(false), 
      _vpDataCallback(nullptr), _rtcDataCallback(nullptr) {
    memset(_dmtBuffer, 0, DMT_BUFFER_SIZE);
}

// Initialization
void DMT_Display::begin(unsigned long baudRate, int rxPin, int txPin) {
    _serial->begin(baudRate, SERIAL_8N1, rxPin, txPin);
    _bufferIndex = 0;
    _frameStarted = false;
}

// Callback setters
void DMT_Display::setVPDataCallback(void (*callback)(uint16_t vpAddress, uint16_t vpData)) {
    _vpDataCallback = callback;
}

void DMT_Display::setRTCDataCallback(void (*callback)(uint8_t* rtcData, int length)) {
    _rtcDataCallback = callback;
}

// Function to write to DGUS1 Register (2 byte data)
void DMT_Display::writeRegister(uint8_t regAddress, uint8_t dataHigh, uint8_t dataLow) {
    uint8_t writeRegCommand[] = {
        DMT_HEADER_1, DMT_HEADER_2,    // Header
        0x04,                          // Length (4 bytes after header)
        DMT_CMD_WRITE_REG,             // Write Register command
        regAddress,                    // Register address (1 byte)
        dataHigh,                      // Data high byte
        dataLow                        // Data low byte
    };
    _serial->write(writeRegCommand, sizeof(writeRegCommand));
}

// Function to read from DGUS1 Register (1 byte data only)
uint8_t DMT_Display::readRegister(uint8_t regAddress) {
    uint8_t readRegCommand[] = {
        DMT_HEADER_1, DMT_HEADER_2,    // Header
        0x03,                          // Length (3 bytes after header)
        DMT_CMD_READ_RTC,              // Read Register command
        regAddress,                    // Register address (1 byte)
        0x01                           // Read 1 byte
    };
    
    _serial->write(readRegCommand, sizeof(readRegCommand));
    return 0; // Placeholder - actual reading handled in callback
}

// Function to write volume (0-100) to VP address
void DMT_Display::writeVP(uint16_t vpAddress, int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    uint16_t vpData = ((uint16_t)volume << 8); // High byte is volume, low byte is 0x00
    
    uint8_t writeVPCommand[] = {
        DMT_HEADER_1, DMT_HEADER_2,    // Header
        0x05,                          // Length
        DMT_CMD_WRITE_VP,              // Write VP command (Variable SRAM)
        (uint8_t)(vpAddress >> 8),     // VP address high byte
        (uint8_t)(vpAddress & 0xFF),   // VP address low byte
        (uint8_t)(vpData >> 8),        // Data high byte (volume)
        (uint8_t)(vpData & 0xFF)       // Data low byte (0x00)
    };
    _serial->write(writeVPCommand, sizeof(writeVPCommand));
}

// Function to write raw VP data to VP address
void DMT_Display::writeVP(uint16_t vpAddress, uint16_t vpData) {
    uint8_t writeVPCommand[] = {
        DMT_HEADER_1, DMT_HEADER_2,    // Header
        0x05,                          // Length
        DMT_CMD_WRITE_VP,              // Write VP command (Variable SRAM)
        (uint8_t)(vpAddress >> 8),     // VP address high byte
        (uint8_t)(vpAddress & 0xFF),   // VP address low byte
        (uint8_t)(vpData >> 8),        // Data high byte
        (uint8_t)(vpData & 0xFF)       // Data low byte
    };
    _serial->write(writeVPCommand, sizeof(writeVPCommand));
}

// Function to write ASCII text to DMT VP address (GBK encoding, 1 byte per character)
void DMT_Display::writeText(uint16_t vpAddress, const char* text) {
    if (text == nullptr) return;
    
    int textLen = strlen(text);
    if (textLen == 0) return;
    
    // Calculate frame length: header(2) + length(1) + command(1) + VP_addr(2) + text_data
    int frameLen = 3 + 1 + 2 + textLen; // 3 for header+length, 1 for command, 2 for VP address, textLen for text
    
    uint8_t* writeTextCommand = new uint8_t[frameLen];
    
    writeTextCommand[0] = DMT_HEADER_1;               // Header
    writeTextCommand[1] = DMT_HEADER_2;               // Header
    writeTextCommand[2] = 1 + 2 + textLen;           // Length: command(1) + VP(2) + textLen
    writeTextCommand[3] = DMT_CMD_WRITE_VP;           // Write VP command
    writeTextCommand[4] = (uint8_t)(vpAddress >> 8);  // VP address high byte
    writeTextCommand[5] = (uint8_t)(vpAddress & 0xFF); // VP address low byte
    
    // Copy ASCII text data (no terminator)
    for (int i = 0; i < textLen; i++) {
        writeTextCommand[6 + i] = (uint8_t)text[i];
    }
    
    _serial->write(writeTextCommand, frameLen);
    delete[] writeTextCommand;
}

// Function to write single ASCII character to DMT VP address
void DMT_Display::writeChar(uint16_t vpAddress, char character) {
    char text[2] = {character, '\0'};
    writeText(vpAddress, text);
}

// Function to read data from DMT VP address (2 bytes data)
uint16_t DMT_Display::readVP(uint16_t vpAddress) {
    uint8_t readVPCommand[] = {
        DMT_HEADER_1, DMT_HEADER_2,    // Header
        0x04,                          // Length
        DMT_CMD_READ_VP,               // Read VP command (Variable SRAM)
        (uint8_t)(vpAddress >> 8),     // VP address high byte
        (uint8_t)(vpAddress & 0xFF),   // VP address low byte
        0x01                           // Read 1 word (2 bytes)
    };
    
    _serial->write(readVPCommand, sizeof(readVPCommand));
    return 0; // Placeholder - actual reading handled in callback
}

// Function to map gain (0.0-1.0) to VP data (high byte = volume_converted, low byte = 0x00)
uint16_t DMT_Display::mapGainToVP(float gain) {
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

// Function to calculate high byte value from gain using reverse formula
uint8_t DMT_Display::calculateHighByteFromGain(float gain) {
    if (gain <= 0.0f) return 0x00;
    if (gain >= 1.0f) return 0x64;  // 100 decimal = 0x64
    
    // Reverse formula: gain = (2^(volume/10))/1000
    // So: volume = 10 * log2(gain * 1000)
    float volume = 10.0f * log2f(gain * 1000.0f);
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 100.0f) volume = 100.0f;
    
    uint8_t highByte = (uint8_t)round(volume);
    return highByte;
}

// Function to map VP data to volume percentage
int DMT_Display::mapVPToVolume(uint16_t vpData) {
    // VP data range: 0x100 (256) to 0x164 (356) = 100 steps
    if (vpData < VP_MIN_VALUE) vpData = VP_MIN_VALUE;  // 0x100 = 256
    if (vpData > VP_MAX_VALUE) vpData = VP_MAX_VALUE;  // 0x164 = 356
    
    // Map VP data (256-356) to volume (0-100)
    int volume = map(vpData, VP_MIN_VALUE, VP_MAX_VALUE, VOLUME_MIN, VOLUME_MAX);
    return volume;
}

// Function to handle incoming UART data
void DMT_Display::handleIncomingData() {
    while (_serial->available()) {
        uint8_t incomingByte = _serial->read();
        
        // Look for frame start (0x5A 0xA5)
        if (!_frameStarted) {
            if (_bufferIndex == 0 && incomingByte == DMT_HEADER_1) {
                _dmtBuffer[_bufferIndex++] = incomingByte;
            } else if (_bufferIndex == 1 && incomingByte == DMT_HEADER_2) {
                _dmtBuffer[_bufferIndex++] = incomingByte;
                _frameStarted = true;
            } else {
                _bufferIndex = 0; // Reset if header not found
            }
        } else {
            // Frame started, collect data
            if (_bufferIndex < DMT_BUFFER_SIZE) {
                _dmtBuffer[_bufferIndex++] = incomingByte;
                
                // Check if we have received length byte (3rd byte)
                if (_bufferIndex == 3) {
                    uint8_t frameLength = _dmtBuffer[2] + 3; // Length + header(2) + length byte(1)
                    if (frameLength > DMT_BUFFER_SIZE) {
                        _bufferIndex = 0;
                        _frameStarted = false;
                        return;
                    }
                }
                
                // Check if we have received the complete frame
                if (_bufferIndex >= 3) {
                    uint8_t expectedFrameLength = _dmtBuffer[2] + 3;
                    if (_bufferIndex >= expectedFrameLength) {
                        // Complete frame received
                        processDMTFrame(_dmtBuffer, _bufferIndex);
                        _bufferIndex = 0;
                        _frameStarted = false;
                    }
                }
            } else {
                _bufferIndex = 0;
                _frameStarted = false;
            }
        }
    }
}

// Function to process complete DMT frame
void DMT_Display::processDMTFrame(uint8_t* frame, int frameLength) {
    if (frameLength < 4) return; // Minimum frame size: header(2) + length(1) + command(1)
    
    uint8_t command = frame[3];
    
    switch (command) {
        case DMT_CMD_READ_VP: // 0x83 - VP data
            if (frameLength >= 8) {
                uint16_t vpAddress = (frame[4] << 8) | frame[5];
                uint16_t vpData = (frame[6] << 8) | frame[7];
                
                // Call user callback if set
                if (_vpDataCallback) {
                    _vpDataCallback(vpAddress, vpData);
                }
            }
            break;
            
        case DMT_CMD_READ_RTC: // 0x81 - RTC data
            if (frameLength >= 5) {
                // Call user callback if set
                if (_rtcDataCallback) {
                    _rtcDataCallback(&frame[4], frameLength - 4);
                }
            }
            break;
            
        case DMT_CMD_WRITE_VP: // 0x82 - Write VP
            // No action needed for write commands
            break;
            
        default:
            // Unknown command
            break;
    }
}

// WiFi status display helpers
void DMT_Display::showWiFiIcon(bool isConnected) {
    uint8_t wifiCommand[] = {
        DMT_HEADER_1, DMT_HEADER_2,    // Header
        0x05,                          // Length (5 bytes after header)
        DMT_CMD_WRITE_VP,              // Write VP command
        0x20, 0x00,                    // VP address 0x2000 (WiFi icon)
        0x00, static_cast<uint8_t>(isConnected ? 0x01 : 0x00) // Data (WiFi ON/OFF)
    };
    _serial->write(wifiCommand, sizeof(wifiCommand));
}

void DMT_Display::showConnectionStatus(const char* message, uint16_t vpAddress) {
    writeText(vpAddress, message);
}

void DMT_Display::showConnectionError(const char* message, uint16_t vpAddress) {
    writeText(vpAddress, message);
}

void DMT_Display::clearText(uint16_t vpAddress, int numChars) {
    // Create a string with specified number of spaces
    char* spaces = new char[numChars + 1];
    for (int i = 0; i < numChars; i++) {
        spaces[i] = ' ';
    }
    spaces[numChars] = '\0';
    
    writeText(vpAddress, spaces);
    delete[] spaces;
}

void DMT_Display::showRSSI(int rssi, uint16_t vpAddress) {
    String rssiMsg = "RSSI=" + String(rssi);
    writeText(vpAddress, rssiMsg.c_str());
}

// System status display
void DMT_Display::showBootMessage(const char* message) {
    writeText(0x3100, message);
}

void DMT_Display::showSystemReady() {
    writeText(0x3100, "System Ready");
}
