#include "slide_detect.h"

#include <cstring>

namespace pocket {

void SlideDetector::reduce(const uint8_t* gray, size_t w, size_t h, uint8_t* out,
                           uint8_t& mean) const {
  const size_t gw = cfg_.gridW, gh = cfg_.gridH;
  uint64_t total = 0;
  for (size_t gy = 0; gy < gh; ++gy) {
    const size_t y0 = gy * h / gh, y1 = (gy + 1) * h / gh;
    for (size_t gx = 0; gx < gw; ++gx) {
      const size_t x0 = gx * w / gw, x1 = (gx + 1) * w / gw;
      uint32_t sum = 0, n = 0;
      for (size_t y = y0; y < y1; ++y) {
        const uint8_t* row = gray + y * w;
        for (size_t x = x0; x < x1; ++x) { sum += row[x]; ++n; }
      }
      const uint8_t m = n ? static_cast<uint8_t>(sum / n) : 0;
      out[gy * gw + gx] = m;
      total += m;
    }
  }
  mean = static_cast<uint8_t>(total / (gw * gh));
}

float SlideDetector::diffFraction(const uint8_t* a, const uint8_t* b) const {
  const size_t n = static_cast<size_t>(cfg_.gridW) * cfg_.gridH;
  size_t changed = 0;
  for (size_t i = 0; i < n; ++i) {
    const int d = static_cast<int>(a[i]) - static_cast<int>(b[i]);
    if (d > cfg_.cellThreshold || d < -cfg_.cellThreshold) ++changed;
  }
  return static_cast<float>(changed) / static_cast<float>(n);
}

bool SlideDetector::update(const uint8_t* gray, size_t w, size_t h, uint32_t nowMs) {
  if (static_cast<size_t>(cfg_.gridW) * cfg_.gridH > kMaxCells || w < cfg_.gridW ||
      h < cfg_.gridH) {
    return false;
  }
  uint8_t mean = 0;
  reduce(gray, w, h, cur_, mean);
  const size_t n = static_cast<size_t>(cfg_.gridW) * cfg_.gridH;

  bool fire = false;
  if (mean >= cfg_.minMeanLuma) {
    const bool stable = havePrev_ && diffFraction(cur_, prev_) < cfg_.stableFraction;
    stableCount_ = stable ? static_cast<uint8_t>(stableCount_ + 1) : 0;
    lastRefChange_ = haveRef_ ? diffFraction(cur_, ref_) : 1.0f;
    const bool isNew = !haveRef_ || lastRefChange_ >= cfg_.changeFraction;
    const bool intervalOk = !everCaptured_ || nowMs - lastCaptureMs_ >= cfg_.minIntervalMs;
    if (isNew && stableCount_ >= cfg_.stableFrames && intervalOk) {
      fire = true;
      std::memcpy(ref_, cur_, n);
      haveRef_ = true;
      everCaptured_ = true;
      lastCaptureMs_ = nowMs;
      stableCount_ = 0;
    }
  } else {
    stableCount_ = 0;
  }
  std::memcpy(prev_, cur_, n);
  havePrev_ = true;
  return fire;
}

void SlideDetector::markCaptured(uint32_t nowMs) {
  if (havePrev_) {
    std::memcpy(ref_, prev_, static_cast<size_t>(cfg_.gridW) * cfg_.gridH);
    haveRef_ = true;
  }
  everCaptured_ = true;
  lastCaptureMs_ = nowMs;
}

}  // namespace pocket
