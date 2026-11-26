// Battery and storage estimates.
#pragma once
#include <cstdint>

namespace pocket {

// LiPo resting-voltage to state-of-charge, piecewise linear. Rough: voltage sags under load.
uint8_t lipoPercent(float volts);

constexpr float kLowBatteryCutoffV = 3.45f;   // stop recording and sleep
constexpr float kLowBatteryResumeV = 3.60f;   // allowed to wake back into Home

// Hours of audio that fit into `freeBytes` at `bytesPerSecond`.
float hoursOfAudio(uint64_t freeBytes, uint32_t bytesPerSecond);

constexpr uint32_t kPcmBytesPerSecond = 16000 * 2;          // 16 kHz, 16-bit mono
constexpr uint32_t kAdpcmBytesPerSecond = 16000 * 256 / 505;  // ~8110

}  // namespace pocket
