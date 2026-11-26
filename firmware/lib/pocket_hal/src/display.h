// WeAct 2.13" black/white e-ink (SSD1680, 250x122) drawn in landscape with GxEPD2.
#pragma once
#include <Arduino.h>

#include "pocket_features.h"

#if FEAT_DISPLAY

enum class Screen : uint8_t { Home, Recording, Sessions, Sync, Settings, UsbDrive, LowBattery, Sleep };

struct SessionLine {
  String label;      // "11-26 08:50"
  uint32_t minutes;  // duration
  bool synced;
};

struct UiModel {
  Screen screen = Screen::Home;
  String clock = "--:--";      // HH:MM, or --:-- when the clock was never set
  String date;                 // "Thu 26 Nov"
  uint8_t batteryPct = 0;
  float freeHours = 0;
  uint16_t unsynced = 0;
  // recording
  String recStart;             // "08:50"
  uint32_t recSeconds = 0;
  uint16_t marks = 0, photos = 0;
  bool lowAudio = false;       // input level suspiciously low (mic blocked?)
  // camera / light
  bool hasCamera = false;
  bool privacy = false;
  bool docked = false;
  String lightProfile;         // "indoor"
  float lux = 0;
  // sessions / sync / settings
  SessionLine sessions[5];
  uint8_t sessionCount = 0;
  String syncLine1, syncLine2;
  uint16_t syncDone = 0, syncTotal = 0;
  String settings[5];
  String footer;               // hint line
};

class Display {
 public:
  bool begin();
  // Draws the model. Uses a partial refresh unless `full` or too many partials piled up.
  void render(const UiModel& m, bool full = false);
  void hibernate();

 private:
  void drawHome(const UiModel& m);
  void drawRecording(const UiModel& m);
  void drawSessions(const UiModel& m);
  void drawSync(const UiModel& m);
  void drawSettings(const UiModel& m);
  void drawBig(const char* title, const char* line1, const char* line2);
  void drawStatusColumn(const UiModel& m);
  uint8_t partials_ = 0;
  bool awake_ = false;
};

#endif
