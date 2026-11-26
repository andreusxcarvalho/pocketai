#include "config.h"

#include <ArduinoJson.h>

#include "storage.h"

bool DeviceConfig::load(Storage& storage) {
  String text;
  if (!storage.readText("/config.json", text)) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, text);
  if (err) {
    log_e("config.json: %s", err.c_str());
    return false;
  }
  deviceName = doc["device_name"] | deviceName.c_str();
  wifiSsid = doc["wifi_ssid"] | "";
  wifiPass = doc["wifi_pass"] | "";
  server = doc["server"] | server.c_str();
  token = doc["token"] | "";
  timezone = doc["timezone"] | timezone.c_str();
  micGain = doc["mic_gain"] | micGain;
  idleSleepMs = doc["idle_sleep_s"] | (idleSleepMs / 1000);
  idleSleepMs *= 1000;
  cleanupPercent = doc["cleanup_percent"] | cleanupPercent;
  autoSlides = doc["auto_slides"] | autoSlides;
  debugLdr = doc["debug_ldr"] | debugLdr;
  ldr.r10 = doc["ldr"]["r10"] | ldr.r10;
  ldr.gamma = doc["ldr"]["gamma"] | ldr.gamma;
  ldr.rFixed = doc["ldr"]["r_fixed"] | ldr.rFixed;
  return true;
}
