#include "sensors.h"

#include "battery.h"
#include "board.h"

int LightSensor::pinFor() { return board::kLdrAdc; }

float LightSensor::readVolts() {
  uint32_t mv = 0;
  for (int i = 0; i < 8; ++i) {
    mv += analogReadMilliVolts(board::kLdrAdc);
    delayMicroseconds(2500);
  }
  return mv / 8.0f / 1000.0f;
}

void Battery::begin() {
  pinMode(board::kBattAdc, INPUT);
  analogSetPinAttenuation(board::kBattAdc, ADC_11db);
}

float Battery::readVolts() {
  uint32_t mv = 0;
  for (int i = 0; i < 16; ++i) mv += analogReadMilliVolts(board::kBattAdc);
  return mv / 16.0f / 1000.0f * board::kBattDivider;
}

uint8_t Battery::percent() { return pocket::lipoPercent(readVolts()); }
