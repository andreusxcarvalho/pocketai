// Device settings, read from /config.json on the SD card (see docs/config.example.json).
#pragma once
#include <Arduino.h>

#include "light.h"

class Storage;

struct DeviceConfig {
  String deviceName = "pocket";
  String wifiSsid;
  String wifiPass;
  String server = "http://pocket-inbox.local:8000";  // Pi inbox base URL
  String token;                                      // shared secret for the inbox
  String timezone = "SGT-8";                         // POSIX TZ string
  float micGain = 4.0f;
  uint32_t idleSleepMs = 120000;
  uint8_t cleanupPercent = 80;                       // delete oldest synced sessions above this
  pocket::LdrCalibration ldr;
  bool autoSlides = true;
  bool debugLdr = false;                             // print LDR volts every second (calibration)

  bool load(Storage& storage);  // missing file = defaults; returns true if a file was read
  bool hasWifi() const { return wifiSsid.length() > 0; }
};
