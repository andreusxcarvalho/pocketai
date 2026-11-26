// Pin map for the Seeed XIAO ESP32S3 Sense build (see docs/wiring.md).
// Verify against Seeed's pinout before soldering.
#pragma once
#include <Arduino.h>

namespace board {

// Touch pads (copper tape behind the case wall)
constexpr int kPadRec = 1;    // D0 / GPIO1 / TOUCH1
constexpr int kPadMode = 2;   // D1 / GPIO2 / TOUCH2

// Analog inputs (ADC1)
constexpr int kLdrAdc = 3;    // D2 / GPIO3  - LDR divider (3V3 - GL5528 - node - 47k - GND)
constexpr int kBattAdc = 4;   // D3 / GPIO4  - BAT+ - 220k - node - 220k - GND

// E-ink (WeAct 2.13" SSD1680), shares SPI with the SD card
constexpr int kEpdCs = 5;     // D4
constexpr int kEpdRst = 6;    // D5
constexpr int kEpdDc = 43;    // D6 (UART TX; logs go over USB)
constexpr int kEpdBusy = 44;  // D7 (UART RX)

// Shared SPI bus
constexpr int kSpiSck = 7;    // D8
constexpr int kSpiMiso = 8;   // D9
constexpr int kSpiMosi = 9;   // D10

// Sense board
constexpr int kSdCs = 21;     // also the user LED
constexpr int kMicClk = 42;   // PDM clock
constexpr int kMicData = 41;  // PDM data
constexpr int kUserLed = 21;  // shared with SD CS: only touch it when the card is idle

// Camera (CAMERA_MODEL_XIAO_ESP32S3)
constexpr int kCamPwdn = -1, kCamReset = -1, kCamXclk = 10, kCamSiod = 40, kCamSioc = 39;
constexpr int kCamY9 = 48, kCamY8 = 11, kCamY7 = 12, kCamY6 = 14, kCamY5 = 16, kCamY4 = 18,
              kCamY3 = 17, kCamY2 = 15, kCamVsync = 38, kCamHref = 47, kCamPclk = 13;

// Divider ratios
constexpr float kBattDivider = 2.0f;

}  // namespace board

// One recursive lock for everything on the shared SPI bus (SD card + e-ink).
// Recursive so a task holding it can call helpers that also take it.
void busLockInit();
void busLock();
void busUnlock();

struct BusGuard {
  BusGuard() { busLock(); }
  ~BusGuard() { busUnlock(); }
  BusGuard(const BusGuard&) = delete;
  BusGuard& operator=(const BusGuard&) = delete;
};
