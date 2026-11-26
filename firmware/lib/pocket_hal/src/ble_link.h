// Bluetooth LE link for the phone/laptop companion (companion/pocket_companion.py).
// Service 7a1e0001-...: status (read/notify, JSON) and control (write, text commands):
//   "rec" "stop" "mark" "sync" "time:<unix epoch>" "privacy"
#pragma once
#include <Arduino.h>

#include "pocket_features.h"

#if FEAT_BLE

class BleLink {
 public:
  void begin(const char* deviceName);
  void end();
  // Next command written by the companion, or "" if none.
  String takeCommand();
  void publishStatus(const String& json);  // updates the value and notifies if subscribed
  bool connected() const;
};

#endif
