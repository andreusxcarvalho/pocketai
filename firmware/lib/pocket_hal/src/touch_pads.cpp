#include "touch_pads.h"

#include "board.h"

using pocket::GestureConfig;

void TouchPads::begin(bool withMode) {
  withMode_ = withMode;
  GestureConfig rc;      // REC: tap = start / bookmark, hold 1.5 s = stop
  rc.holdMs = 1500;
  rec_ = pocket::GestureDetector(rc);
  GestureConfig mc;      // MODE: tap = next screen, hold 2 s = sync, hold 5 s = privacy
  mc.holdMs = 2000;
  mc.longHoldMs = 5000;
  mode_ = pocket::GestureDetector(mc);
  // Seed baselines with a short average so the first touch is recognised.
  uint32_t r = 0, m = 0;
  for (int i = 0; i < 8; ++i) {
    r += touchRead(board::kPadRec);
    if (withMode_) m += touchRead(board::kPadMode);
    delay(5);
  }
  rec_.setBaseline(r / 8.0f);
  if (withMode_) mode_.setBaseline(m / 8.0f);
}

PadEvents TouchPads::poll(uint32_t now) {
  PadEvents ev;
  lastRec_ = touchRead(board::kPadRec);
  ev.rec = rec_.update(lastRec_, now);
  if (withMode_) {
    lastMode_ = touchRead(board::kPadMode);
    ev.mode = mode_.update(lastMode_, now);
  }
  return ev;
}

bool TouchPads::anyTouched() const { return rec_.touching() || mode_.touching(); }

void TouchPads::armWake() {
  // ESP32-S3: wake when the reading rises past the threshold.
  const uint32_t threshold = static_cast<uint32_t>(rec_.baseline() * 1.08f);
  touchSleepWakeUpEnable(board::kPadRec, threshold);
}
