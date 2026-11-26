// OV3660 / OV2640 / OV5640 camera on the Sense board, driven by esp32-camera.
// The sensor stays in JPEG mode; small frames for slide detection are decoded on the chip.
#pragma once
#include <Arduino.h>

#include <vector>

#include "pocket_features.h"
#include "light.h"

#if FEAT_CAMERA

struct CaptureResult {
  bool ok = false;
  uint16_t width = 0, height = 0;
  size_t bytes = 0;
  uint32_t exposure = 0, gain = 0;
};

class Camera {
 public:
  bool begin();
  void end();
  bool ready() const { return ready_; }
  const char* sensorName() const;

  // Applies the light profile, discards warm-up frames, captures a JPEG and writes it to
  // `path` on the SD card.
  CaptureResult captureToFile(const pocket::CameraSettings& s, const String& path);

  // Grabs a 160x120 frame and converts it to 8-bit grayscale (for slide detection).
  bool captureGray(std::vector<uint8_t>& gray, uint16_t& w, uint16_t& h);

  // Software standby between shots (best effort; see docs/hardware-bringup.md).
  void standby(bool on);

 private:
  void apply(const pocket::CameraSettings& s);
  void readExposure(uint32_t& exposure, uint32_t& gain);
  bool ready_ = false;
  uint16_t pid_ = 0;
};

#endif
