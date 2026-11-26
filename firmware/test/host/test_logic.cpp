#include <cstring>
#include <vector>

#include "battery.h"
#include "gesture.h"
#include "light.h"
#include "minitest.h"
#include "session.h"
#include "slide_detect.h"
#include "state_machine.h"

using namespace pocket;

// ---------- light ----------
TEST(ldr_resistance_and_lux_are_monotonic) {
  LdrCalibration cal;
  CHECK_NEAR(ldrResistance(1.65f, cal), 47000.0, 1.0);  // half supply = equal resistors
  CHECK_NEAR(luxFromResistance(cal.r10, cal), 10.0, 0.01);
  CHECK(luxFromVolts(3.0f, cal) > luxFromVolts(2.5f, cal));
  CHECK(luxFromVolts(0.0f, cal) == 0.0f);
}

TEST(light_profile_hysteresis) {
  LightClassifier c(30000, 0.13f);
  CHECK(c.update(400, 0) == LightProfile::Indoor);
  CHECK(c.update(140, 100) == LightProfile::Indoor);  // within margin below 150
  CHECK(c.update(125, 200) == LightProfile::Dim);     // below 150 * 0.87
  CHECK(c.update(160, 300) == LightProfile::Dim);     // not above 150 * 1.13
  CHECK(c.update(180, 400) == LightProfile::Indoor);
  CHECK(c.update(5000, 500) == LightProfile::Bright);
  CHECK(c.update(3, 600) == LightProfile::Dark);      // multi-band jump down
}

TEST(light_docked_after_30s_of_darkness) {
  LightClassifier c(30000);
  c.update(0.2f, 0);
  CHECK(!c.docked());
  c.update(0.2f, 29000);
  CHECK(!c.docked());
  CHECK(c.update(0.2f, 30500) == LightProfile::Covered);
  CHECK(c.docked());
  c.update(300, 31000);
  CHECK(!c.docked());
}

TEST(profile_settings_match_brief) {
  CHECK(!settingsFor(LightProfile::Covered).capture);
  CHECK_EQ(settingsFor(LightProfile::Dark).gainCeilingX, uint8_t(8));
  CHECK(settingsFor(LightProfile::Dark).lowLightFlag);
  CHECK(settingsFor(LightProfile::Indoor).wb == WhiteBalance::Office);
  CHECK_EQ(settingsFor(LightProfile::Bright).aeLevel, int8_t(-1));
  CHECK(settingsFor(LightProfile::Indoor).warmupFrames < settingsFor(LightProfile::Dark).warmupFrames);
}

// ---------- gestures ----------
static Gesture feed(GestureDetector& g, uint32_t raw, uint32_t from, uint32_t to, uint32_t stepMs = 20) {
  Gesture last = Gesture::None;
  for (uint32_t t = from; t <= to; t += stepMs) {
    Gesture e = g.update(raw, t);
    if (e != Gesture::None) last = e;
  }
  return last;
}

TEST(gesture_tap_hold_brush) {
  GestureConfig cfg;
  cfg.longHoldMs = 5000;
  GestureDetector g(cfg);
  feed(g, 1000, 0, 200);                       // settle baseline
  CHECK(feed(g, 1200, 220, 400) == Gesture::None);
  CHECK(g.update(1000, 420) == Gesture::Tap);  // ~200 ms touch = tap

  feed(g, 1200, 500, 520);                     // 20-40 ms brush
  CHECK(g.update(1000, 540) == Gesture::None);

  CHECK(feed(g, 1200, 1000, 2600) == Gesture::Hold);
  CHECK(g.update(1000, 2620) == Gesture::None); // release after hold: no tap

  CHECK(feed(g, 1200, 3000, 4600) == Gesture::Hold);
  CHECK(feed(g, 1200, 4620, 8100) == Gesture::LongHold);
}

TEST(gesture_falling_polarity) {
  GestureConfig cfg;
  cfg.risesWhenTouched = false;
  GestureDetector g(cfg);
  feed(g, 1000, 0, 100);
  feed(g, 800, 120, 300);
  CHECK(g.update(1000, 320) == Gesture::Tap);
}

// ---------- state machine ----------
TEST(state_record_bookmark_photo_stop) {
  Context ctx;
  ctx.caps.camera = true;
  auto t = step(State::Home, Event::RecTap, ctx);
  CHECK(t.next == State::Recording && (t.actions & kStartRecording));
  t = step(State::Recording, Event::RecTap, ctx);
  CHECK((t.actions & kBookmark) && (t.actions & kTakePhoto));
  ctx.docked = true;
  t = step(State::Recording, Event::RecTap, ctx);
  CHECK((t.actions & kBookmark) && !(t.actions & kTakePhoto));
  ctx.docked = false;
  t = step(State::Recording, Event::ModeLongHold, ctx);
  CHECK(ctx.privacy);
  t = step(State::Recording, Event::RecTap, ctx);
  CHECK(!(t.actions & kTakePhoto));
  t = step(State::Recording, Event::IdleTimeout, ctx);
  CHECK(t.next == State::Recording);
  t = step(State::Recording, Event::RecHold, ctx);
  CHECK(t.next == State::Home && (t.actions & kStopRecording));
}

TEST(state_sync_only_when_supported_and_idle) {
  Context ctx;
  auto t = step(State::Home, Event::ModeHold, ctx);
  CHECK(t.next == State::Home && (t.actions & kRejected));
  ctx.caps.sync = true;
  t = step(State::Home, Event::ModeHold, ctx);
  CHECK(t.next == State::Syncing && (t.actions & kStartSync));
  t = step(State::Recording, Event::ModeHold, ctx);
  CHECK(t.next == State::Recording && (t.actions & kRejected));
  t = step(State::Syncing, Event::SyncDone, ctx);
  CHECK(t.next == State::Home);
}

TEST(state_low_battery_saves_recording) {
  Context ctx;
  auto t = step(State::Recording, Event::BatteryLow, ctx);
  CHECK(t.next == State::LowBattery);
  CHECK((t.actions & kStopRecording) && (t.actions & kEnterSleep));
  t = step(State::LowBattery, Event::BatteryOk, ctx);
  CHECK(t.next == State::Home);
}

TEST(state_usb_drive) {
  Context ctx;
  ctx.caps.usbDrive = true;
  auto t = step(State::Home, Event::UsbAttached, ctx);
  CHECK(t.next == State::UsbDrive && (t.actions & kStartUsbDrive));
  t = step(State::UsbDrive, Event::RecTap, ctx);
  CHECK(t.next == State::UsbDrive);
  t = step(State::UsbDrive, Event::UsbDetached, ctx);
  CHECK(t.next == State::Home && (t.actions & kStopUsbDrive));
}

// ---------- slide detection ----------
static std::vector<uint8_t> frame(uint8_t bg, int boxX = -1, uint8_t box = 0) {
  std::vector<uint8_t> f(160 * 120, bg);
  if (boxX >= 0)
    for (int y = 20; y < 100; ++y)
      for (int x = boxX; x < boxX + 60; ++x) f[y * 160 + x] = box;
  return f;
}

TEST(slide_captures_after_change_settles) {
  SlideDetector d;
  auto a = frame(200, 10, 40);
  int fired = 0;
  for (uint32_t t = 0; t < 4000; t += 1000) fired += d.update(a.data(), 160, 120, t);
  CHECK_EQ(fired, 1);  // first stable view becomes slide 1
  auto b = frame(200, 90, 40);
  CHECK(!d.update(b.data(), 160, 120, 10000));  // changed, not yet stable
  CHECK(!d.update(b.data(), 160, 120, 11000));  // stable 1
  CHECK(d.update(b.data(), 160, 120, 12000));   // stable 2 -> capture
  CHECK(!d.update(b.data(), 160, 120, 13000));  // same slide: no repeat
}

TEST(slide_ignores_dark_and_respects_interval) {
  SlideDetectConfig cfg;
  cfg.minIntervalMs = 20000;
  SlideDetector d(cfg);
  auto dark = frame(5);
  for (uint32_t t = 0; t < 5000; t += 1000) CHECK(!d.update(dark.data(), 160, 120, t));
  auto a = frame(200, 10, 40), b = frame(200, 90, 40);
  int fired = 0;
  for (uint32_t t = 6000; t < 10000; t += 1000) fired += d.update(a.data(), 160, 120, t);
  CHECK_EQ(fired, 1);
  for (uint32_t t = 10000; t < 20000; t += 1000) fired += d.update(b.data(), 160, 120, t);
  CHECK_EQ(fired, 1);  // blocked by 20 s interval
  for (uint32_t t = 20000; t < 30000; t += 1000) fired += d.update(b.data(), 160, 120, t);
  CHECK_EQ(fired, 2);
}

// ---------- battery / session ----------
TEST(battery_curve) {
  CHECK_EQ(lipoPercent(4.25f), uint8_t(100));
  CHECK_EQ(lipoPercent(3.2f), uint8_t(0));
  CHECK_EQ(lipoPercent(3.80f), uint8_t(50));
  CHECK(lipoPercent(3.85f) > 50 && lipoPercent(3.85f) < 65);
  CHECK_NEAR(hoursOfAudio(115200000ull, kPcmBytesPerSecond), 1.0, 0.001);
}

TEST(session_names_and_json) {
  DateTime t{2026, 11, 26, 8, 50, 3};
  CHECK(sessionDirName(t, true, 7, 2) == "2026-11-26_0850");
  CHECK(sessionDirName(t, false, 7, 2) == "boot0007_s002");
  CHECK(isoTimestamp(t) == "2026-11-26T08:50:03");
  CHECK(photoName(12) == "img_0012.jpg");

  std::vector<Mark> marks{{1503200, "img_0001.jpg", "tap"}, {1600000, "", "tap"}};
  CHECK(marksJson(marks) ==
        "[{\"t_ms\":1503200,\"photo\":\"img_0001.jpg\",\"source\":\"tap\"},"
        "{\"t_ms\":1600000,\"photo\":null,\"source\":\"tap\"}]");

  PhotoMeta p;
  p.session = "2026-11-26_0850";
  p.lux = 312.44f;
  p.profile = LightProfile::Indoor;
  p.sensor = "OV3660";
  const std::string pj = photoJson(p);
  CHECK(pj.find("\"lux\":312.4") != std::string::npos);
  CHECK(pj.find("\"profile\":\"indoor\"") != std::string::npos);

  SessionMeta s;
  s.id = "x\"y";
  s.status = "recording";
  const std::string sj = sessionJson(s);
  CHECK(sj.find("\"id\":\"x\\\"y\"") != std::string::npos);
  CHECK(sj.find("\"started_at\":null") != std::string::npos);
  CHECK(sj.find("\"clock\":\"estimated\"") != std::string::npos);
}

int main() { return minitest::runAll(); }
