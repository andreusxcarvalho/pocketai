#include "app.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "audio_in.h"
#include "battery.h"
#include "board.h"
#include "config.h"
#include "power.h"
#include "ring_buffer.h"
#include "sensors.h"
#include "storage.h"
#include "touch_pads.h"

#if FEAT_DISPLAY
#include "display.h"
#endif
#if FEAT_CAMERA
#include "camera.h"
#include "slide_detect.h"
#endif
#if FEAT_SYNC
#include "sync_client.h"
#endif
#if FEAT_USB_DRIVE
#include <USB.h>
#include "usb_drive.h"
#endif
#if FEAT_BLE
#include "ble_link.h"
#endif
#if FEAT_WATCHDOG
#include <esp_task_wdt.h>
#endif

using namespace pocket;

App app;

namespace {

constexpr uint32_t kSampleRate = 16000;
constexpr size_t kRingBytes = 256 * 1024;  // ~8 s of PCM, in PSRAM
constexpr uint32_t kRecRedrawMs = 30000;   // e-ink partial refresh cadence while recording

AudioIn audio;
Storage storage;
DeviceConfig cfg;
TouchPads pads;
Battery battery;
RingBuffer ring;
WavRecorder recorder;

#if FEAT_DISPLAY
Display display;
UiModel ui;
#endif
#if FEAT_LDR
LightSensor ldr;
LightClassifier light;
float lastLux = 300.0f;
#endif
#if FEAT_CAMERA
Camera camera;
SlideDetector slides;
struct CamReq {
  uint8_t kind;      // 0 = photo for mark, 1 = slide probe
  uint32_t markIdx;
  uint32_t offsetMs;
  char source[8];
};
QueueHandle_t camQueue = nullptr;
#endif
#if FEAT_USB_DRIVE
UsbDrive usbDrive;
#endif
#if FEAT_BLE
BleLink ble;
uint32_t lastBlePublish = 0;
#endif

// Shared between tasks
volatile bool recording = false;     // audio task pushes while true
volatile bool finalizeReq = false;   // loop -> storage: drain and close
volatile bool finalized = true;      // storage -> loop: file closed
volatile int16_t lastPeak = 0;
SemaphoreHandle_t sessionMutex = nullptr;

struct ActiveSession {
  String id;
  String dir;
  uint32_t startMs = 0;
  std::string startedAt;
  std::vector<Mark> marks;
  uint32_t photos = 0;
} session;

struct Guard {
  Guard() { xSemaphoreTake(sessionMutex, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(sessionMutex); }
};

uint32_t quietSince = 0;
bool lowAudio = false;
uint32_t lastSensors = 0, lastBattery = 0, lastSlideProbe = 0;

WavRecorder::Format audioFormat() {
  return FEAT_ADPCM ? WavRecorder::Format::ImaAdpcm : WavRecorder::Format::Pcm16;
}
uint32_t bytesPerSecond() { return FEAT_ADPCM ? kAdpcmBytesPerSecond : kPcmBytesPerSecond; }

// Below ~2.5 V there is no cell on the BAT pads (bench use on USB power): never "low".
bool batteryLow(float v) { return v > 2.5f && v < kLowBatteryCutoffV; }

}  // namespace

// ------------------------------------------------------------------ tasks

void App::audioTask(void*) {
  static int16_t buf[512];
  for (;;) {
    if (!recording || !audio.running()) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    const size_t n = audio.read(buf, 512);
    if (n) {
      ring.write(reinterpret_cast<const uint8_t*>(buf), n * sizeof(int16_t));
      lastPeak = audio.peak();
    }
  }
}

void App::storageTask(void*) {
  static int16_t chunk[4096];
#if FEAT_WATCHDOG
  esp_task_wdt_add(nullptr);
#endif
  for (;;) {
#if FEAT_WATCHDOG
    esp_task_wdt_reset();
#endif
    if (!recorder.isOpen()) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    size_t got = ring.read(reinterpret_cast<uint8_t*>(chunk), sizeof chunk);
    if (got) {
      recorder.write(chunk, got / sizeof(int16_t));
      recorder.maybeCheckpoint(millis());
    } else if (finalizeReq && !recording) {
      recorder.close();
      finalizeReq = false;
      finalized = true;
    } else {
      vTaskDelay(pdMS_TO_TICKS(15));
    }
  }
}

#if FEAT_CAMERA
void App::cameraTask(void*) {
  CamReq req;
  std::vector<uint8_t> gray;
  for (;;) {
    if (xQueueReceive(camQueue, &req, portMAX_DELAY) != pdTRUE) continue;
    if (!camera.ready()) continue;
    bool takePhoto = req.kind == 0;
#if FEAT_AUTO_SLIDES
    if (req.kind == 1) {
      uint16_t w, h;
      if (!camera.captureGray(gray, w, h)) continue;
      if (!slides.update(gray.data(), w, h, millis())) continue;
      // New, settled slide: record an automatic mark and take the photo.
      Guard g;
      if (!recording) continue;
      Mark m;
      m.tMs = millis() - session.startMs;
      m.source = "auto";
      session.marks.push_back(m);
      req.markIdx = session.marks.size() - 1;
      req.offsetMs = m.tMs;
      strncpy(req.source, "auto", sizeof req.source);
      takePhoto = true;
    }
#endif
    if (!takePhoto) continue;

    float lux = 300.0f;
    LightProfile profile = LightProfile::Indoor;
#if FEAT_LDR
    lux = ldr.readLux();       // fresh reading for the sidecar
    profile = light.current(); // classifier is owned by the loop task (updated every second)
    if (profile == LightProfile::Covered) continue;
#endif
    CameraSettings st = settingsFor(profile);
    String dir, id;
    uint32_t photoIdx;
    {
      Guard g;
      dir = session.dir;
      id = session.id;
      photoIdx = ++session.photos;
    }
    const std::string name = photoName(photoIdx);
    CaptureResult r = camera.captureToFile(st, dir + "/" + name.c_str());
    if (!r.ok) {
      Guard g;
      --session.photos;
      continue;
    }
#if FEAT_AUTO_SLIDES
    if (req.kind == 0) slides.markCaptured(millis());
#endif
    PhotoMeta meta;
    meta.session = id.c_str();
    meta.offsetMs = req.offsetMs;
    meta.lux = lux;
    meta.profile = profile;
    meta.exposure = r.exposure;
    meta.gain = r.gain;
    meta.warmupFrames = st.warmupFrames;
    meta.lowLight = st.lowLightFlag;
    meta.width = r.width;
    meta.height = r.height;
    meta.sensor = camera.sensorName();
    meta.source = req.source;
    String sidecar = dir + "/" + String(name.c_str());
    sidecar.replace(".jpg", ".json");
    storage.writeText(sidecar, photoJson(meta));
    {
      Guard g;
      if (req.markIdx < session.marks.size()) session.marks[req.markIdx].photo = name;
    }
    app.writeMarks();
  }
}
#else
void App::cameraTask(void*) { vTaskDelete(nullptr); }
#endif

// ------------------------------------------------------------------ setup / loop

void App::setup() {
  Serial.begin(115200);
#if FEAT_USB_DRIVE
  usbDrive.begin();
  USB.begin();
#endif
  busLockInit();
  sessionMutex = xSemaphoreCreateMutex();
  power::noteBoot();
  const power::WakeReason wake = power::wakeReason();
  log_i("Pocket %s %s, boot %u", pocketVersionName(), POCKET_FW_SEMVER, power::bootCount());

  if (storage.begin()) {
    cfg.load(storage);
    const int fixed = storage.repairIncompleteSessions();
    if (fixed) log_w("repaired %d interrupted session(s)", fixed);
  }
  clockx::begin(cfg.timezone.c_str());
  audio.setGain(cfg.micGain);

  uint8_t* mem = static_cast<uint8_t*>(ps_malloc(kRingBytes));
  if (!mem) mem = static_cast<uint8_t*>(malloc(32 * 1024));  // no PSRAM: smaller buffer
  ring.init(mem, mem ? (psramFound() ? kRingBytes : 32 * 1024) : 0);

  pads.begin(FEAT_MODE_PAD);
  ctx_.caps.camera = FEAT_CAMERA;
  ctx_.caps.sync = FEAT_SYNC;
  ctx_.caps.usbDrive = FEAT_USB_DRIVE;

#if FEAT_BATTERY
  battery.begin();
#endif
#if FEAT_LDR
  ldr.begin(cfg.ldr);
#endif
#if FEAT_DISPLAY
  display.begin();
#endif
#if FEAT_CAMERA
  if (camera.begin()) camera.standby(true);
  camQueue = xQueueCreate(6, sizeof(CamReq));
  xTaskCreatePinnedToCore(cameraTask, "camera", 8192, nullptr, 3, nullptr, 0);
#endif
#if FEAT_BLE
  ble.begin(cfg.deviceName.c_str());
#endif

  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 6, nullptr, 1);
  xTaskCreatePinnedToCore(storageTask, "storage", 8192, nullptr, 5, nullptr, 0);
#if FEAT_WATCHDOG
  enableLoopWDT();
#endif

#if FEAT_BATTERY
  if (batteryLow(battery.readVolts())) {
    handle(Event::BatteryLow);
    return;
  }
#endif
  state_ = State::Home;
  if (wake != power::WakeReason::PowerOn) handle(Event::Wake);
  lastInteraction_ = millis();
  redraw(true);
#if !FEAT_DISPLAY
  blink(2);  // v01: two blinks = ready
#endif
}

void App::loop() {
  const uint32_t now = millis();
  const PadEvents ev = pads.poll(now);
  if (pads.anyTouched()) lastInteraction_ = now;

  if (ev.rec == Gesture::Tap) handle(Event::RecTap);
  if (ev.rec == Gesture::Hold) {
#if FEAT_USB_DRIVE
    if (state_ == State::UsbDrive) handle(Event::UsbDetached);  // force exit
    else
#endif
      handle(Event::RecHold);
  }
  if (ev.mode == Gesture::Tap) handle(Event::ModeTap);
  if (ev.mode == Gesture::Hold) handle(Event::ModeHold);
  if (ev.mode == Gesture::LongHold) handle(Event::ModeLongHold);

  pollSensors(now);
  pollBle(now);
  pollUsb();

#if FEAT_SLEEP
  if (state_ == State::Home && now - lastInteraction_ > cfg.idleSleepMs) handle(Event::IdleTimeout);
#endif
  if (state_ == State::Recording && now - lastRedraw_ > kRecRedrawMs) redraw(false);
  delay(20);  // ~50 Hz touch polling
}

// ------------------------------------------------------------------ state machine glue

void App::handle(Event e) {
  const State before = state_;
  Transition t = step(state_, e, ctx_);
  state_ = t.next;
  if (t.actions != kNone) {
    log_i("%s + %s -> %s (0x%03x)", stateName(before), eventName(e), stateName(state_), t.actions);
  }
  run(t.actions);
}

void App::run(uint32_t a) {
  if (a & kStartRecording) {
    if (!startRecording()) {
      state_ = State::Home;
#if FEAT_DISPLAY
      ui.footer = storage.mounted() ? "Couldn't start recording" : "No SD card";
#endif
    }
    screenIdx_ = 0;
  }
  if (a & kBookmark) bookmark(a & kTakePhoto, "tap");
  if (a & kStopRecording) {
    stopRecording();
    screenIdx_ = 0;
  }
  if (a & kNextScreen) nextScreen();
  if (a & kTogglePrivacy) {
#if FEAT_CAMERA
    if (ctx_.privacy) camera.standby(true);
#endif
  }
  if (a & kStartSync) doSync();
#if FEAT_USB_DRIVE
  if (a & kStartUsbDrive) {
    storage.end();
    if (!usbDrive.start()) {
      storage.begin();
      state_ = State::Home;
    }
  }
  if (a & kStopUsbDrive) {
    usbDrive.stop();
    storage.begin();
  }
#endif
  if (a & kShowLowBattery) {
#if FEAT_DISPLAY
    ui.screen = Screen::LowBattery;
    display.render(ui, true);
#endif
  }
  if (a & kEnterSleep) sleepNow();
  if (a & kRedraw) redraw(true);
}

// ------------------------------------------------------------------ recording

bool App::startRecording() {
  if (!storage.mounted() || !finalized) return false;
  const DateTime t = clockx::now();
  Guard g;
  session = ActiveSession();
  session.id = sessionDirName(t, clockx::synced(), power::bootCount(), sessionIndex_++).c_str();
  session.dir = "/sessions/" + session.id;
  session.startedAt = clockx::synced() ? isoTimestamp(t) : "";
  if (!storage.mkdirs(session.dir)) return false;
  if (!audio.begin(kSampleRate)) return false;
  ring.clear();  // producer is idle here, so this is safe
  finalized = false;
  finalizeReq = false;
  if (!recorder.open(session.dir + "/audio.wav", audioFormat(), kSampleRate)) {
    audio.end();
    finalized = true;
    return false;
  }
  session.startMs = millis();
  writeSessionMeta("recording");
  quietSince = 0;
  recording = true;
  return true;
}

void App::stopRecording() {
  recording = false;
  finalizeReq = true;
  const uint32_t start = millis();
  while (!finalized && millis() - start < 5000) delay(10);
  delay(50);  // let the audio task finish any read in flight before the driver goes away
  audio.end();
  writeMarks();
  writeSessionMeta(finalized ? "complete" : "recording");
  lowAudio = false;
#if !FEAT_DISPLAY
  blink(3);  // v01: three blinks = saved
#endif
}

void App::bookmark(bool withPhoto, const char* source) {
  uint32_t idx, offset;
  {
    Guard g;
    Mark m;
    m.tMs = millis() - session.startMs;
    m.source = source;
    session.marks.push_back(m);
    idx = session.marks.size() - 1;
    offset = m.tMs;
  }
  writeMarks();
#if FEAT_CAMERA
  if (withPhoto && camQueue) {
    CamReq r{};
    r.kind = 0;
    r.markIdx = idx;
    r.offsetMs = offset;
    strncpy(r.source, source, sizeof r.source - 1);
    xQueueSend(camQueue, &r, 0);
  }
#else
  (void)withPhoto;
  (void)idx;
  (void)offset;
#endif
}

void App::writeMarks() {
  std::string json;
  String dir;
  {
    Guard g;
    if (session.dir.isEmpty()) return;
    json = marksJson(session.marks);
    dir = session.dir;
  }
  storage.writeText(dir + "/marks.json", json);
}

void App::writeSessionMeta(const char* status) {
  SessionMeta m;
  m.id = session.id.c_str();
  m.startedAt = session.startedAt;
  m.clockSynced = clockx::synced();
  m.firmware = std::string(pocketVersionName()) + " " + POCKET_FW_SEMVER;
  m.audioFormat = FEAT_ADPCM ? "ima-adpcm" : "pcm16";
  m.sampleRate = kSampleRate;
  m.durationMs = recorder.durationMs();
  m.status = status;
  m.marks = session.marks.size();
  m.photos = session.photos;
  m.overruns = ring.overruns();
  storage.writeText(session.dir + "/session.json", sessionJson(m));
}

// ------------------------------------------------------------------ ui

void App::nextScreen() {
#if FEAT_DISPLAY
  const uint8_t homeScreens = FEAT_OTA ? 3 : 2;  // Home, Sessions (+ Settings on v10)
  screenIdx_ = (screenIdx_ + 1) % (state_ == State::Recording ? 2 : homeScreens);
#endif
}

void App::redraw(bool full) {
  lastRedraw_ = millis();
#if FEAT_DISPLAY
  ui.clock = clockx::hhmm();
  ui.date = clockx::dateLine();
  ui.batteryPct = battery.percent();
  ui.freeHours = hoursOfAudio(storage.freeBytes(), bytesPerSecond());
  ui.hasCamera = FEAT_CAMERA;
  ui.privacy = ctx_.privacy;
  ui.docked = ctx_.docked;
#if FEAT_LDR
  ui.lightProfile = profileName(light.current());
  ui.lux = lastLux;
#endif
#if FEAT_SYNC
  ui.unsynced = SyncClient::countUnsynced(storage);
#endif
  {
    Guard g;
    ui.recSeconds = (millis() - session.startMs) / 1000;
    ui.marks = session.marks.size();
    ui.photos = session.photos;
    ui.recStart = session.startedAt.empty() ? String("--:--")
                                            : String(session.startedAt.substr(11, 5).c_str());
  }
  ui.lowAudio = lowAudio;

  switch (state_) {
    case State::Recording: ui.screen = screenIdx_ == 0 ? Screen::Recording : Screen::Sessions; break;
    case State::Syncing: ui.screen = Screen::Sync; break;
    case State::UsbDrive: ui.screen = Screen::UsbDrive; break;
    case State::LowBattery: ui.screen = Screen::LowBattery; break;
    case State::Sleep: ui.screen = Screen::Sleep; break;
    case State::Home:
      ui.screen = screenIdx_ == 0 ? Screen::Home : (screenIdx_ == 1 ? Screen::Sessions : Screen::Settings);
      break;
  }
  if (ui.screen == Screen::Sessions) {
    auto ids = storage.listSessions();
#if FEAT_SYNC
    auto synced = SyncClient::loadSynced(storage);
#endif
    ui.sessionCount = 0;
    for (int i = static_cast<int>(ids.size()) - 1; i >= 0 && ui.sessionCount < 5; --i) {
      SessionLine& l = ui.sessions[ui.sessionCount++];
      const String& id = ids[i];
      l.label = id.length() >= 15 ? id.substring(5, 10) + " " + id.substring(11, 13) + ":" + id.substring(13, 15) : id;
      String meta;
      l.minutes = 0;
      if (storage.readText("/sessions/" + id + "/session.json", meta)) {
        int d = meta.indexOf("\"duration_ms\":");
        if (d >= 0) l.minutes = meta.substring(d + 14).toInt() / 60000;
      }
#if FEAT_SYNC
      l.synced = synced.count(id) > 0;
#else
      l.synced = false;
#endif
    }
  }
  if (ui.screen == Screen::Settings) {
    ui.settings[0] = String(pocketVersionName()) + " " + POCKET_FW_SEMVER;
    ui.settings[1] = "Device " + cfg.deviceName + ", boot " + String(power::bootCount());
    ui.settings[2] = "Mic gain x" + String(cfg.micGain, 1);
#if FEAT_LDR
    ui.settings[3] = "Light " + String(lastLux, 0) + " lux, " + profileName(light.current());
#endif
    ui.settings[4] = cfg.hasWifi() ? "Wi-Fi " + cfg.wifiSsid : String("Wi-Fi not set");
  }
  display.render(ui, full);
#else
  (void)full;
#endif
}

void App::blink(int times) {
  if (recorder.isOpen()) return;  // LED shares the SD chip-select: only when the card is idle
  BusGuard g;
  pinMode(board::kUserLed, OUTPUT);
  for (int i = 0; i < times; ++i) {
    digitalWrite(board::kUserLed, LOW);
    delay(120);
    digitalWrite(board::kUserLed, HIGH);
    delay(150);
  }
}

// ------------------------------------------------------------------ background work

void App::pollSensors(uint32_t now) {
  // Microphone sanity: a covered or failed mic reads near silence for a long time.
  if (state_ == State::Recording) {
    if (lastPeak < 150) {
      if (!quietSince) quietSince = now;
      lowAudio = now - quietSince > 10000;
    } else {
      quietSince = 0;
      lowAudio = false;
    }
  }
#if FEAT_LDR
  if (now - lastSensors >= 1000) {
    lastSensors = now;
    const float volts = ldr.readVolts();
    lastLux = luxFromVolts(volts, ldr.calibration());
    if (cfg.debugLdr) Serial.printf("ldr %.3f V %.1f lux\n", volts, lastLux);
    const bool wasDocked = ctx_.docked;
    light.update(lastLux, now);
    ctx_.docked = light.docked();
    if (wasDocked != ctx_.docked && state_ == State::Recording) redraw(false);
  }
#endif
#if FEAT_AUTO_SLIDES
  if (state_ == State::Recording && cfg.autoSlides && !ctx_.privacy && !ctx_.docked &&
      now - lastSlideProbe >= 5000 && camQueue) {
    lastSlideProbe = now;
    CamReq r{};
    r.kind = 1;
    xQueueSend(camQueue, &r, 0);
  }
#endif
#if FEAT_BATTERY
  if (now - lastBattery >= 60000) {
    lastBattery = now;
    if (batteryLow(battery.readVolts())) handle(Event::BatteryLow);
  }
#endif
#if FEAT_WATCHDOG
  feedLoopWDT();
#endif
}

void App::pollBle(uint32_t now) {
#if FEAT_BLE
  String cmd = ble.takeCommand();
  if (cmd.length()) {
    lastInteraction_ = now;
    if (cmd == "rec" && state_ == State::Home) handle(Event::RecTap);
    else if (cmd == "mark" && state_ == State::Recording) handle(Event::RecTap);
    else if (cmd == "stop" && state_ == State::Recording) handle(Event::RecHold);
    else if (cmd == "sync") handle(Event::ModeHold);
    else if (cmd == "privacy") handle(Event::ModeLongHold);
    else if (cmd.startsWith("time:")) {
      clockx::setEpoch(cmd.substring(5).toInt());
      redraw(false);
    }
    lastBlePublish = 0;  // publish fresh status right away
  }
  if (now - lastBlePublish >= 2000) {
    lastBlePublish = now;
    String js = "{\"state\":\"" + String(stateName(state_)) + "\"";
    js += ",\"rec_s\":" + String(state_ == State::Recording ? (now - session.startMs) / 1000 : 0);
    js += ",\"marks\":" + String(session.marks.size());
    js += ",\"photos\":" + String(session.photos);
    js += ",\"battery\":" + String(battery.percent());
    js += ",\"privacy\":" + String(ctx_.privacy ? "true" : "false");
    js += ",\"docked\":" + String(ctx_.docked ? "true" : "false");
    js += ",\"clock\":" + String(clockx::synced() ? "true" : "false");
    js += ",\"fw\":\"" + String(pocketVersionName()) + "\"}";
    ble.publishStatus(js);
  }
#else
  (void)now;
#endif
}

void App::pollUsb() {
#if FEAT_USB_DRIVE
  static bool wasHost = false;
  const bool host = usbDrive.hostConnected();
  if (host && !wasHost && state_ == State::Home && cfg.usbDriveAuto) handle(Event::UsbAttached);
  if (state_ == State::UsbDrive && (!host || usbDrive.ejected())) handle(Event::UsbDetached);
  wasHost = host;
#endif
}

void App::doSync() {
#if FEAT_SYNC
#if FEAT_WATCHDOG
  disableLoopWDT();  // uploads can block for minutes
#endif
  SyncClient sync;
  auto progress = [&](const String& l1, const String& l2, uint16_t done, uint16_t total) {
#if FEAT_DISPLAY
    ui.screen = Screen::Sync;
    ui.syncLine1 = l1;
    ui.syncLine2 = l2;
    ui.syncDone = done;
    ui.syncTotal = total;
    display.render(ui, false);
#endif
    log_i("sync: %s %s %u/%u", l1.c_str(), l2.c_str(), done, total);
  };
  SyncReport r = sync.run(storage, cfg, progress);
#if FEAT_OTA
  if (r.wifi && r.error.isEmpty()) sync.checkOta(cfg, progress);
#endif
  delay(1500);  // let the result line be read
#if FEAT_WATCHDOG
  enableLoopWDT();
#endif
#endif
  handle(Event::SyncDone);
}

void App::sleepNow() {
#if FEAT_DISPLAY
  ui.screen = state_ == State::LowBattery ? Screen::LowBattery : Screen::Sleep;
  ui.footer = clockx::synced() ? "Last seen " + clockx::hhmm() : String("");
  display.render(ui, true);
  display.hibernate();
#endif
#if FEAT_CAMERA
  camera.standby(true);
#endif
#if FEAT_BLE
  ble.end();
#endif
  storage.end();
  pads.armWake();
  power::deepSleep(0);
}
