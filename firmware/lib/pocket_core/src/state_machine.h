// Device state machine. Pure logic: events in, new state + actions out.
// The firmware executes the actions; host tests check the transitions.
#pragma once
#include <cstdint>

namespace pocket {

enum class State : uint8_t { Sleep, Home, Recording, Syncing, UsbDrive, LowBattery };

enum class Event : uint8_t {
  Wake,           // woke from deep sleep (touch or timer)
  RecTap,
  RecHold,        // 1.5 s
  ModeTap,
  ModeHold,       // 2 s
  ModeLongHold,   // 5 s
  IdleTimeout,
  BatteryLow,     // below cutoff
  BatteryOk,      // checked on wake: above cutoff + margin
  SyncDone,
  UsbAttached,
  UsbDetached,
};

// Bit flags for what the firmware should do.
enum Action : uint32_t {
  kNone = 0,
  kStartRecording = 1u << 0,
  kStopRecording = 1u << 1,
  kBookmark = 1u << 2,
  kTakePhoto = 1u << 3,
  kNextScreen = 1u << 4,
  kStartSync = 1u << 5,
  kTogglePrivacy = 1u << 6,
  kEnterSleep = 1u << 7,
  kShowLowBattery = 1u << 8,
  kStartUsbDrive = 1u << 9,
  kStopUsbDrive = 1u << 10,
  kRedraw = 1u << 11,
  kRejected = 1u << 12,  // event understood but not allowed now (e.g. sync while recording)
};

struct Capabilities {
  bool camera = false;
  bool sync = false;
  bool usbDrive = false;
};

struct Context {
  Capabilities caps;
  bool privacy = false;   // camera disabled by the user
  bool docked = false;    // LDR says covered (on phone / in pocket)
};

struct Transition {
  State next;
  uint32_t actions;
};

Transition step(State s, Event e, Context& ctx);
const char* stateName(State s);
const char* eventName(Event e);

}  // namespace pocket
