#include "power.h"

#include <esp_sleep.h>
#include <sys/time.h>
#include <time.h>

RTC_DATA_ATTR static uint32_t rtcBootCount = 0;
RTC_DATA_ATTR static bool rtcClockSynced = false;

namespace power {

WakeReason wakeReason() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_UNDEFINED: return WakeReason::PowerOn;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: return WakeReason::Touch;
    case ESP_SLEEP_WAKEUP_TIMER: return WakeReason::Timer;
    default: return WakeReason::Other;
  }
}

uint32_t bootCount() { return rtcBootCount; }

void noteBoot() {
  if (wakeReason() == WakeReason::PowerOn) rtcClockSynced = false;  // time is gone after power loss
  ++rtcBootCount;
}

void deepSleep(uint32_t timerSeconds) {
  if (timerSeconds) esp_sleep_enable_timer_wakeup(uint64_t(timerSeconds) * 1000000ULL);
  Serial.flush();
  esp_deep_sleep_start();
  while (true) {}
}

}  // namespace power

namespace clockx {

void begin(const char* tz) {
  setenv("TZ", tz, 1);
  tzset();
}

bool synced() { return rtcClockSynced; }

bool syncNtp(uint32_t timeoutMs) {
  configTzTime(getenv("TZ") ? getenv("TZ") : "UTC0", "pool.ntp.org", "time.google.com");
  struct tm t;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (getLocalTime(&t, 500) && t.tm_year > (2024 - 1900)) {
      rtcClockSynced = true;
      return true;
    }
  }
  return false;
}

void setEpoch(uint32_t epoch) {
  struct timeval tv = {static_cast<time_t>(epoch), 0};
  settimeofday(&tv, nullptr);
  rtcClockSynced = true;
}

pocket::DateTime now() {
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  pocket::DateTime d;
  d.year = static_cast<uint16_t>(lt.tm_year + 1900);
  d.month = static_cast<uint8_t>(lt.tm_mon + 1);
  d.day = static_cast<uint8_t>(lt.tm_mday);
  d.hour = static_cast<uint8_t>(lt.tm_hour);
  d.minute = static_cast<uint8_t>(lt.tm_min);
  d.second = static_cast<uint8_t>(lt.tm_sec);
  return d;
}

String hhmm() {
  if (!synced()) return "--:--";
  char b[8];
  auto d = now();
  snprintf(b, sizeof b, "%02u:%02u", d.hour, d.minute);
  return b;
}

String dateLine() {
  if (!synced()) return "Clock not set";
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  char b[24];
  strftime(b, sizeof b, "%a %d %b", &lt);
  return b;
}

}  // namespace clockx
