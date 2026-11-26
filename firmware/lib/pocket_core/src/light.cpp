#include "light.h"

#include <cmath>

namespace pocket {

float ldrResistance(float v, const LdrCalibration& cal) {
  if (v <= 0.001f) return 1e9f;            // no light: effectively open circuit
  if (v >= cal.vcc - 0.001f) return 1.0f;  // saturated
  return cal.rFixed * (cal.vcc - v) / v;
}

float luxFromResistance(float ohms, const LdrCalibration& cal) {
  if (ohms >= 1e8f) return 0.0f;
  return 10.0f * std::pow(cal.r10 / ohms, 1.0f / cal.gamma);
}

const char* profileName(LightProfile p) {
  switch (p) {
    case LightProfile::Covered: return "covered";
    case LightProfile::Dark: return "dark";
    case LightProfile::Dim: return "dim";
    case LightProfile::Indoor: return "indoor";
    case LightProfile::Bright: return "bright";
  }
  return "?";
}

CameraSettings settingsFor(LightProfile p) {
  CameraSettings s;
  switch (p) {
    case LightProfile::Covered:
      s.capture = false;
      break;
    case LightProfile::Dark:
      s.aec2 = true; s.gainCeilingX = 8; s.frameSize = FrameSize::SVGA_800x600;
      s.warmupFrames = 6; s.lowLightFlag = true;
      break;
    case LightProfile::Dim:
      s.aec2 = true; s.gainCeilingX = 4; s.wb = WhiteBalance::Auto;
      s.frameSize = FrameSize::UXGA_1600x1200; s.warmupFrames = 4;
      break;
    case LightProfile::Indoor:
      s.gainCeilingX = 2; s.wb = WhiteBalance::Office;
      s.frameSize = FrameSize::QXGA_2048x1536; s.warmupFrames = 2;
      break;
    case LightProfile::Bright:
      s.gainCeilingX = 2; s.aeLevel = -1; s.wb = WhiteBalance::Sunny;
      s.frameSize = FrameSize::QXGA_2048x1536; s.warmupFrames = 2;
      break;
  }
  return s;
}

namespace {
// Boundaries between Dark|Dim, Dim|Indoor, Indoor|Bright (lux).
const float kBounds[3] = {10.0f, 150.0f, 1000.0f};
constexpr float kCoveredLux = 1.0f;
}  // namespace

LightProfile LightClassifier::update(float lux, uint32_t nowMs) {
  // Docked / pocket detection.
  if (lux < kCoveredLux) {
    if (!darkTiming_) {
      darkTiming_ = true;
      darkSince_ = nowMs;
    } else if (!covered_ && nowMs - darkSince_ >= coveredMs_) {
      covered_ = true;
    }
  } else if (lux > kCoveredLux * 1.5f) {
    darkTiming_ = false;
    covered_ = false;
  }

  // Band with hysteresis. Band index: 0 Dark, 1 Dim, 2 Indoor, 3 Bright.
  int band = static_cast<int>(band_) - static_cast<int>(LightProfile::Dark);
  bool moved = true;
  while (moved) {
    moved = false;
    if (band < 3 && lux > kBounds[band] * (1.0f + hyst_)) {
      ++band;
      moved = true;
    } else if (band > 0 && lux < kBounds[band - 1] * (1.0f - hyst_)) {
      --band;
      moved = true;
    }
  }
  band_ = static_cast<LightProfile>(band + static_cast<int>(LightProfile::Dark));
  return current();
}

}  // namespace pocket
