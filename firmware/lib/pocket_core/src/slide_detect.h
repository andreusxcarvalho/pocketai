// Detects "the slide changed and has settled" from small grayscale frames.
// Frames (e.g. 160x120 from the camera's GRAYSCALE QQVGA mode) are reduced to a grid of
// cell means. A capture fires once the scene differs from the last captured slide AND has
// been stable for a few frames, so the photo isn't taken mid-transition.
#pragma once
#include <cstddef>
#include <cstdint>

namespace pocket {

struct SlideDetectConfig {
  uint8_t gridW = 16, gridH = 12;
  uint8_t cellThreshold = 14;       // mean-luma difference that counts a cell as changed
  float changeFraction = 0.15f;     // share of cells changed vs. last capture to call it new
  float stableFraction = 0.03f;     // frame-to-frame change below this counts as stable
  uint8_t stableFrames = 2;         // consecutive stable frames needed before capture
  uint32_t minIntervalMs = 8000;    // never capture more often than this
  uint8_t minMeanLuma = 20;         // too dark: ignore
};

class SlideDetector {
 public:
  static constexpr size_t kMaxCells = 32 * 24;
  explicit SlideDetector(const SlideDetectConfig& cfg = SlideDetectConfig()) : cfg_(cfg) {}

  // Returns true when a new, settled slide should be captured now.
  bool update(const uint8_t* gray, size_t width, size_t height, uint32_t nowMs);
  // Call after a capture made for another reason (manual tap) so it becomes the reference.
  void markCaptured(uint32_t nowMs);
  float lastChangeVsReference() const { return lastRefChange_; }

 private:
  void reduce(const uint8_t* gray, size_t w, size_t h, uint8_t* out, uint8_t& mean) const;
  float diffFraction(const uint8_t* a, const uint8_t* b) const;

  SlideDetectConfig cfg_;
  uint8_t ref_[kMaxCells] = {0};
  uint8_t prev_[kMaxCells] = {0};
  uint8_t cur_[kMaxCells] = {0};
  bool haveRef_ = false, havePrev_ = false;
  uint8_t stableCount_ = 0;
  uint32_t lastCaptureMs_ = 0;
  bool everCaptured_ = false;
  float lastRefChange_ = 0.0f;
};

}  // namespace pocket
