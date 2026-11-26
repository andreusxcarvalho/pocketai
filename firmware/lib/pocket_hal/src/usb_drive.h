// Exposes the SD card as a USB mass-storage drive (TinyUSB MSC), so recordings show up in
// Finder/Explorer or the iPhone Files app. The firmware must not touch the filesystem while
// the drive is shared: Storage is unmounted first and remounted after.
#pragma once
#include <Arduino.h>

#include "pocket_features.h"

#if FEAT_USB_DRIVE

class UsbDrive {
 public:
  // Registers USB event callbacks; call once in setup() before USB.begin().
  void begin();
  bool hostConnected() const;   // a computer/phone enumerated us (not just a charger)
  bool start();                 // SD must already be unmounted by the caller
  void stop();
  bool active() const { return active_; }
  bool ejected() const;         // host ejected the disk

 private:
  bool active_ = false;
};

#endif
