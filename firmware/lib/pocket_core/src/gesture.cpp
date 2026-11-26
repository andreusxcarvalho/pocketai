#include "gesture.h"

namespace pocket {

Gesture GestureDetector::update(uint32_t raw, uint32_t nowMs) {
  const float v = static_cast<float>(raw);
  if (baseline_ <= 0.0f) baseline_ = v;

  // Distance from baseline in the "touched" direction, as a fraction of the baseline.
  const float delta = cfg_.risesWhenTouched ? (v - baseline_) : (baseline_ - v);
  const float frac = baseline_ > 0.0f ? delta / baseline_ : 0.0f;

  if (!touching_) {
    if (frac > cfg_.triggerRatio) {
      touching_ = true;
      holdFired_ = longFired_ = false;
      downAt_ = nowMs;
      return Gesture::None;
    }
    // Track slow drift (temperature, humidity) only while untouched.
    baseline_ += (v - baseline_) / 64.0f;
    return Gesture::None;
  }

  const uint32_t held = nowMs - downAt_;
  if (frac < cfg_.releaseRatio) {
    touching_ = false;
    if (!holdFired_ && held >= cfg_.minTapMs && held <= cfg_.maxTapMs) return Gesture::Tap;
    return Gesture::None;
  }
  if (!holdFired_ && held >= cfg_.holdMs) {
    holdFired_ = true;
    return Gesture::Hold;
  }
  if (cfg_.longHoldMs > 0 && !longFired_ && held >= cfg_.longHoldMs) {
    longFired_ = true;
    return Gesture::LongHold;
  }
  return Gesture::None;
}

}  // namespace pocket
