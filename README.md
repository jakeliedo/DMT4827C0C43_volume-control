# ESP32-C3 Super Mini + DMT48270C43 UART + HTTP Control Project

This project is for ESP32-C3 Super Mini, communicating with a DMT48270C43 touchscreen via UART. The ESP32 connects to WiFi and sends HTTP requests to a Powersoft Mezzo 604 A device, simulating browser requests. Commands are received from the touchscreen UI and translated into HTTP requests.

## Features
- UART communication with DMT48270C43 touchscreen
- WiFi connection and management
- HTTP client to send commands to Powersoft Mezzo 604 A
- Example code for UART, WiFi, and HTTP

## Getting Started
1. Open this project in PlatformIO (VS Code)
2. Connect ESP32-C3 Super Mini to your PC
3. Update WiFi credentials and Mezzo device IP in `src/main.cpp`
4. Build and upload the firmware

## Hardware
- ESP32-C3 Super Mini
- DMT48270C43 UART touchscreen
- Powersoft Mezzo 604 A (target device)

## Notes
- Ensure UART wiring between ESP32 and DMT48270C43 is correct
- HTTP requests are sent to the Mezzo device as if from a web browser

---
