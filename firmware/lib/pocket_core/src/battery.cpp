#include "battery.h"

namespace pocket {
namespace {
struct Pt { float v; float pct; };
const Pt kCurve[] = {{3.30f, 0},  {3.40f, 2},  {3.50f, 5},  {3.60f, 12}, {3.65f, 20},
                     {3.70f, 30}, {3.75f, 40}, {3.80f, 50}, {3.90f, 65}, {4.00f, 80},
                     {4.10f, 90}, {4.20f, 100}};
constexpr int kN = sizeof(kCurve) / sizeof(kCurve[0]);
}  // namespace

uint8_t lipoPercent(float v) {
  if (v <= kCurve[0].v) return 0;
  if (v >= kCurve[kN - 1].v) return 100;
  for (int i = 1; i < kN; ++i) {
    if (v <= kCurve[i].v) {
      const float t = (v - kCurve[i - 1].v) / (kCurve[i].v - kCurve[i - 1].v);
      return static_cast<uint8_t>(kCurve[i - 1].pct + t * (kCurve[i].pct - kCurve[i - 1].pct) + 0.5f);
    }
  }
  return 100;
}

float hoursOfAudio(uint64_t freeBytes, uint32_t bytesPerSecond) {
  if (bytesPerSecond == 0) return 0.0f;
  return static_cast<float>(freeBytes) / static_cast<float>(bytesPerSecond) / 3600.0f;
}

}  // namespace pocket
