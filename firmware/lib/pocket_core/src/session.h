// Session naming and the JSON files written alongside each recording.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "light.h"

namespace pocket {

struct DateTime {
  uint16_t year = 1970;
  uint8_t month = 1, day = 1, hour = 0, minute = 0, second = 0;
};

// "2026-11-26_0850" when the clock is trusted, else "boot0042_s003".
std::string sessionDirName(const DateTime& t, bool clockSynced, uint32_t bootCount,
                           uint32_t sessionIndex);
std::string isoTimestamp(const DateTime& t);  // "2026-11-26T08:50:00"
std::string photoName(uint32_t index);         // "img_0001.jpg"

struct Mark {
  uint32_t tMs = 0;          // offset into the recording
  std::string photo;         // empty if no photo was taken
  std::string source;        // "tap" or "auto"
};

struct PhotoMeta {
  std::string session;
  uint32_t offsetMs = 0;
  float lux = 0.0f;
  LightProfile profile = LightProfile::Indoor;
  uint32_t exposure = 0;     // sensor AEC value read back after capture
  uint32_t gain = 0;         // sensor AGC value read back after capture
  uint8_t warmupFrames = 0;
  bool lowLight = false;
  uint16_t width = 0, height = 0;
  std::string sensor;        // "OV3660", "OV2640", "OV5640"
  std::string source;        // "tap" or "auto"
};

struct SessionMeta {
  std::string id;
  std::string startedAt;     // ISO time or "" if unknown
  bool clockSynced = false;
  std::string firmware;      // e.g. "v10-full 0.10.0"
  std::string audioFormat;   // "pcm16" or "ima-adpcm"
  uint32_t sampleRate = 16000;
  uint32_t durationMs = 0;
  std::string status;        // "recording" until closed, then "complete" or "repaired"
  uint32_t marks = 0, photos = 0, overruns = 0;
};

std::string marksJson(const std::vector<Mark>& marks);
std::string photoJson(const PhotoMeta& m);
std::string sessionJson(const SessionMeta& m);

}  // namespace pocket
