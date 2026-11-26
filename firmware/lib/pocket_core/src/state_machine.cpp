#include "state_machine.h"

namespace pocket {

Transition step(State s, Event e, Context& ctx) {
  // Low battery wins from any active state.
  if (e == Event::BatteryLow && s != State::Sleep && s != State::LowBattery) {
    uint32_t a = kShowLowBattery | kEnterSleep;
    if (s == State::Recording) a |= kStopRecording;
    if (s == State::UsbDrive) a |= kStopUsbDrive;
    return {State::LowBattery, a};
  }

  switch (s) {
    case State::Sleep:
    case State::LowBattery:
      if (e == Event::Wake || e == Event::BatteryOk) {
        return {State::Home, kRedraw};
      }
      if (e == Event::BatteryLow) return {State::LowBattery, kShowLowBattery | kEnterSleep};
      return {s, kNone};

    case State::Home:
      switch (e) {
        case Event::RecTap: return {State::Recording, kStartRecording | kRedraw};
        case Event::ModeTap: return {State::Home, kNextScreen | kRedraw};
        case Event::ModeHold:
          if (ctx.caps.sync) return {State::Syncing, kStartSync | kRedraw};
          return {State::Home, kRejected};
        case Event::ModeLongHold:
          ctx.privacy = !ctx.privacy;
          return {State::Home, kTogglePrivacy | kRedraw};
        case Event::IdleTimeout: return {State::Sleep, kEnterSleep};
        case Event::UsbAttached:
          if (ctx.caps.usbDrive) return {State::UsbDrive, kStartUsbDrive | kRedraw};
          return {State::Home, kNone};
        default: return {State::Home, kNone};
      }

    case State::Recording:
      switch (e) {
        case Event::RecTap: {
          uint32_t a = kBookmark | kRedraw;
          if (ctx.caps.camera && !ctx.privacy && !ctx.docked) a |= kTakePhoto;
          return {State::Recording, a};
        }
        case Event::RecHold: return {State::Home, kStopRecording | kRedraw};
        case Event::ModeTap: return {State::Recording, kNextScreen | kRedraw};
        case Event::ModeHold: return {State::Recording, kRejected};  // no sync mid-recording
        case Event::ModeLongHold:
          ctx.privacy = !ctx.privacy;
          return {State::Recording, kTogglePrivacy | kRedraw};
        default: return {State::Recording, kNone};  // idle timeout never stops a recording
      }

    case State::Syncing:
      if (e == Event::SyncDone) return {State::Home, kRedraw};
      return {State::Syncing, kNone};

    case State::UsbDrive:
      if (e == Event::UsbDetached) return {State::Home, kStopUsbDrive | kRedraw};
      return {State::UsbDrive, kNone};
  }
  return {s, kNone};
}

const char* stateName(State s) {
  switch (s) {
    case State::Sleep: return "sleep";
    case State::Home: return "home";
    case State::Recording: return "recording";
    case State::Syncing: return "syncing";
    case State::UsbDrive: return "usb-drive";
    case State::LowBattery: return "low-battery";
  }
  return "?";
}

const char* eventName(Event e) {
  switch (e) {
    case Event::Wake: return "wake";
    case Event::RecTap: return "rec-tap";
    case Event::RecHold: return "rec-hold";
    case Event::ModeTap: return "mode-tap";
    case Event::ModeHold: return "mode-hold";
    case Event::ModeLongHold: return "mode-long-hold";
    case Event::IdleTimeout: return "idle-timeout";
    case Event::BatteryLow: return "battery-low";
    case Event::BatteryOk: return "battery-ok";
    case Event::SyncDone: return "sync-done";
    case Event::UsbAttached: return "usb-attached";
    case Event::UsbDetached: return "usb-detached";
  }
  return "?";
}

}  // namespace pocket
