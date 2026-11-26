// LDR maths, light profiles with hysteresis, and docked/pocket detection.
#pragma once
#include <cstdint>

namespace pocket {

struct LdrCalibration {
  float vcc = 3.3f;          // divider supply
  float rFixed = 47000.0f;   // resistor from the ADC node to GND
  float r10 = 15000.0f;      // LDR resistance at 10 lux (fit per part, see tools/ldr_calibrate.py)
  float gamma = 0.65f;       // LDR curve exponent
};

// Divider: VCC - LDR - node - rFixed - GND. Returns LDR resistance in ohms.
float ldrResistance(float nodeVolts, const LdrCalibration& cal);
// Approximate lux from LDR resistance.
float luxFromResistance(float ohms, const LdrCalibration& cal);
inline float luxFromVolts(float v, const LdrCalibration& cal) {
  return luxFromResistance(ldrResistance(v, cal), cal);
}

enum class LightProfile : uint8_t { Covered, Dark, Dim, Indoor, Bright };
const char* profileName(LightProfile p);

// Abstract camera settings; the HAL maps them onto esp32-camera calls.
enum class FrameSize : uint8_t { SVGA_800x600, UXGA_1600x1200, QXGA_2048x1536 };
enum class WhiteBalance : uint8_t { Auto = 0, Sunny = 1, Cloudy = 2, Office = 3, Home = 4 };

struct CameraSettings {
  bool capture = true;           // false for Covered
  bool aec2 = false;             // night-mode auto exposure
  int8_t aeLevel = 0;            // -2..2
  uint8_t gainCeilingX = 2;      // 2, 4, 8, 16...
  WhiteBalance wb = WhiteBalance::Auto;
  FrameSize frameSize = FrameSize::UXGA_1600x1200;
  uint8_t warmupFrames = 4;
  bool lowLightFlag = false;
  uint8_t jpegQuality = 10;      // esp32-camera: lower = better
};

CameraSettings settingsFor(LightProfile p);

// Classifies lux into a profile. Uses +-13% hysteresis around each boundary so a reading
// near a boundary doesn't flip profiles, and reports Covered only after `coveredMs`
// of continuous near-darkness (on the phone, in a pocket).
class LightClassifier {
 public:
  explicit LightClassifier(uint32_t coveredMs = 30000, float hysteresis = 0.13f)
      : coveredMs_(coveredMs), hyst_(hysteresis) {}
  LightProfile update(float lux, uint32_t nowMs);
  LightProfile current() const { return covered_ ? LightProfile::Covered : band_; }
  bool docked() const { return covered_; }

 private:
  uint32_t coveredMs_;
  float hyst_;
  LightProfile band_ = LightProfile::Indoor;
  bool covered_ = false;
  bool darkTiming_ = false;
  uint32_t darkSince_ = 0;
};

}  // namespace pocket
