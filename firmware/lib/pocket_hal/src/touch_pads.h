// REC and MODE capacitive pads (copper tape inside the case edge).
#pragma once
#include <Arduino.h>

#include "gesture.h"

struct PadEvents {
  pocket::Gesture rec = pocket::Gesture::None;
  pocket::Gesture mode = pocket::Gesture::None;
};

class TouchPads {
 public:
  void begin(bool withMode);
  // Call at ~50 Hz.
  PadEvents poll(uint32_t nowMs);
  bool anyTouched() const;
  uint32_t rawRec() const { return lastRec_; }
  uint32_t rawMode() const { return lastMode_; }
  // Arms the REC pad as a deep-sleep wake source (threshold derived from its baseline).
  void armWake();

 private:
  pocket::GestureDetector rec_;
  pocket::GestureDetector mode_;
  bool withMode_ = false;
  uint32_t lastRec_ = 0, lastMode_ = 0;
};
