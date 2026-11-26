// PDM microphone on the Sense board, read through the Arduino-ESP32 v3 ESP_I2S driver.
#pragma once
#include <Arduino.h>

class AudioIn {
 public:
  bool begin(uint32_t sampleRate = 16000);
  void end();
  // Blocks until `maxSamples` are read or the driver times out. Applies digital gain.
  size_t read(int16_t* out, size_t maxSamples);
  void setGain(float g) { gain_ = g; }
  // Peak level of the last read, 0..32767 (for the level meter / tests).
  int16_t peak() const { return peak_; }
  bool running() const { return running_; }

 private:
  float gain_ = 4.0f;  // the Sense PDM mic is quiet; tune with tools/ in bring-up
  int16_t peak_ = 0;
  bool running_ = false;
};
