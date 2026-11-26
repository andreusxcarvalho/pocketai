// Deep sleep, wake reasons, boot counter and the wall clock.
#pragma once
#include <Arduino.h>

#include "session.h"

namespace power {

enum class WakeReason { PowerOn, Touch, Timer, Other };

WakeReason wakeReason();
uint32_t bootCount();         // survives deep sleep (RTC memory)
void noteBoot();
// Enters deep sleep. Wake on the REC pad (armed by the caller) and optionally a timer.
[[noreturn]] void deepSleep(uint32_t timerSeconds = 0);

}  // namespace power

namespace clockx {

void begin(const char* posixTz);
bool synced();                     // true once NTP (or BLE/phone) set the time since power-on
bool syncNtp(uint32_t timeoutMs);  // needs Wi-Fi
void setEpoch(uint32_t epoch);     // from the BLE companion
pocket::DateTime now();
String hhmm();
String dateLine();                 // "Thu 26 Nov"

}  // namespace clockx
