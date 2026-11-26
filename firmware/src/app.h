// The Pocket recorder application. One codebase; features switch on per version
// (include/pocket_features.h). Tasks:
//   audio   (core 1, high)  PDM mic -> ring buffer in PSRAM
//   storage (core 0, high)  ring buffer -> WAV/ADPCM on SD, checkpoints every 10 s
//   camera  (core 0, mid)   photo + slide-probe requests from a queue          [v03+]
//   loop    (core 1)        touch, state machine, display, sensors, sync, BLE, USB
#pragma once
#include <Arduino.h>

#include <vector>

#include "pocket_features.h"
#include "session.h"
#include "state_machine.h"

class App {
 public:
  void setup();
  void loop();

 private:
  void handle(pocket::Event e);
  void run(uint32_t actions);

  // recording
  bool startRecording();
  void stopRecording();
  void bookmark(bool withPhoto, const char* source);
  void writeSessionMeta(const char* status);
  void writeMarks();

  // ui
  void redraw(bool full);
  void nextScreen();
  void blink(int times);

  // background
  void pollSensors(uint32_t now);
  void pollBle(uint32_t now);
  void pollUsb();
  void doSync();
  [[noreturn]] void sleepNow();

  static void audioTask(void*);
  static void storageTask(void*);
  static void cameraTask(void*);

  pocket::State state_ = pocket::State::Home;
  pocket::Context ctx_;
  uint32_t lastInteraction_ = 0;
  uint32_t lastRedraw_ = 0;
  uint8_t screenIdx_ = 0;
  uint32_t sessionIndex_ = 0;
};

extern App app;
