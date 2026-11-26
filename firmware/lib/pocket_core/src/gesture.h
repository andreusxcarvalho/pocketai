// Capacitive touch pad gesture detection: tap, hold, long hold.
// Feed raw readings (ESP32-S3 touchRead() rises when touched) at ~50 Hz.
#pragma once
#include <cstdint>

namespace pocket {

enum class Gesture : uint8_t { None, Tap, Hold, LongHold };

struct GestureConfig {
  float triggerRatio = 0.08f;   // touched when raw > baseline * (1 + ratio)
  float releaseRatio = 0.04f;   // released when raw < baseline * (1 + ratio)
  uint32_t minTapMs = 60;       // shorter contacts are brushes from gripping; ignored
  uint32_t maxTapMs = 600;      // longer than this and released before hold = nothing
  uint32_t holdMs = 1500;       // fires Hold once while still touching
  uint32_t longHoldMs = 0;      // 0 = disabled; otherwise fires LongHold once
  bool risesWhenTouched = true; // true on ESP32-S3, false on classic ESP32
};

class GestureDetector {
 public:
  explicit GestureDetector(const GestureConfig& cfg = GestureConfig()) : cfg_(cfg) {}
  // Returns the gesture recognised by this sample, if any.
  Gesture update(uint32_t raw, uint32_t nowMs);
  bool touching() const { return touching_; }
  float baseline() const { return baseline_; }
  void setBaseline(float b) { baseline_ = b; }

 private:
  GestureConfig cfg_;
  float baseline_ = 0.0f;
  bool touching_ = false;
  bool holdFired_ = false;
  bool longFired_ = false;
  uint32_t downAt_ = 0;
};

}  // namespace pocket
