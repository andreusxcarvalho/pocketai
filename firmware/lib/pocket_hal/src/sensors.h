// LDR light sensor and battery voltage (both plain ADC dividers).
#pragma once
#include <Arduino.h>

#include "light.h"

class LightSensor {
 public:
  void begin(const pocket::LdrCalibration& cal) { cal_ = cal; pinMode(pinFor(), INPUT); }
  float readVolts();  // 8-sample average, ~20 ms
  float readLux() { return pocket::luxFromVolts(readVolts(), cal_); }
  const pocket::LdrCalibration& calibration() const { return cal_; }
  void setCalibration(const pocket::LdrCalibration& c) { cal_ = c; }

 private:
  static int pinFor();
  pocket::LdrCalibration cal_;
};

class Battery {
 public:
  void begin();
  float readVolts();
  uint8_t percent();
};
