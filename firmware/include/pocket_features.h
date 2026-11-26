// Which features each product version has. Set POCKET_VERSION in platformio.ini.
//
//  v01 voice-memo     REC pad, WAV to SD card, serial log, LED when idle
//  v02 status-screen  + landscape e-ink UI, MODE pad, battery gauge, deep sleep
//  v03 snapshots      + camera: REC tap while recording = bookmark + photo
//  v04 light-aware    + LDR light profiles, docked/pocket detection
//  v05 wifi-sync      + Wi-Fi upload to the Pi inbox, NTP clock, auto clean-up
//  v06 compact-audio  + IMA-ADPCM recording (about 4x smaller files)
//  v07 auto-slides    + automatic photo when the slide changes
//  v08 usb-drive      + shows up as a USB drive when plugged into a computer/phone
//  v09 ble-companion  + Bluetooth LE status/control for the phone companion
//  v10 full           + OTA updates from the inbox, task watchdog, settings screen
#pragma once

#ifndef POCKET_VERSION
#define POCKET_VERSION 10
#endif

#define FEAT_DISPLAY (POCKET_VERSION >= 2)
#define FEAT_MODE_PAD (POCKET_VERSION >= 2)
#define FEAT_BATTERY (POCKET_VERSION >= 2)
#define FEAT_SLEEP (POCKET_VERSION >= 2)
#define FEAT_CAMERA (POCKET_VERSION >= 3)
#define FEAT_LDR (POCKET_VERSION >= 4)
#define FEAT_SYNC (POCKET_VERSION >= 5)
#define FEAT_ADPCM (POCKET_VERSION >= 6)
#define FEAT_AUTO_SLIDES (POCKET_VERSION >= 7)
#define FEAT_USB_DRIVE (POCKET_VERSION >= 8)
#define FEAT_BLE (POCKET_VERSION >= 9)
#define FEAT_OTA (POCKET_VERSION >= 10)
#define FEAT_WATCHDOG (POCKET_VERSION >= 10)

#define POCKET_FW_SEMVER "0.1.0"

inline const char* pocketVersionName() {
  switch (POCKET_VERSION) {
    case 1: return "v01-voice-memo";
    case 2: return "v02-status-screen";
    case 3: return "v03-snapshots";
    case 4: return "v04-light-aware";
    case 5: return "v05-wifi-sync";
    case 6: return "v06-compact-audio";
    case 7: return "v07-auto-slides";
    case 8: return "v08-usb-drive";
    case 9: return "v09-ble-companion";
    default: return "v10-full";
  }
}
