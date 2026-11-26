# The 10 product versions

One firmware codebase, ten build targets. Each version is a complete product that adds to the
one before it, so you can bring the hardware up one feature at a time and always have a working
recorder. Features are switched in [`firmware/include/pocket_features.h`](../firmware/include/pocket_features.h).

| Env | Version | Adds | Needs |
| --- | --- | --- | --- |
| `v01_voice_memo` | Voice memo | Tap REC to record, hold 1.5 s to stop, tap while recording to bookmark. WAV to SD. LED blinks 2× ready, 3× saved (only when the card is idle; the LED shares the SD chip-select). Crash-safe header every 10 s, repair on boot. | XIAO + Sense, SD card, REC pad |
| `v02_status_screen` | Status screen | Landscape e-ink UI (Home, Recording, Sessions), MODE pad, battery gauge, deep sleep after 2 min idle with REC-pad wake. | + e-ink, MODE pad, battery divider |
| `v03_snapshots` | Snapshots | REC tap while recording = bookmark + photo, with a JSON sidecar per photo. | + camera |
| `v04_light_aware` | Light-aware | LDR picks a camera profile before each shot (fewer warm-up frames, right gain/white balance); "docked" detection switches to audio-only on the phone or in a pocket. | + LDR divider |
| `v05_wifi_sync` | Wi-Fi sync | Hold MODE 2 s: joins Wi-Fi, sets the clock by NTP, uploads finished sessions to the Pi inbox with SHA-256 checks, clears old synced sessions above 80% full. | `/config.json`, Pi inbox |
| `v06_compact_audio` | Compact audio | IMA-ADPCM recording: about 29 MB/hour instead of 115 MB/hour. | — |
| `v07_auto_slides` | Auto slides | Every 5 s while recording, a small frame is compared with the last slide; when it changes and settles, a photo is taken automatically. | — |
| `v08_usb_drive` | USB drive | Plugging into a computer or iPhone (data cable, idle) shows the SD card as a drive "Pocket Recorder". Eject, or hold REC, to leave. | TinyUSB build flag (set) |
| `v09_ble_companion` | BLE companion | Bluetooth LE status/control: `companion/pocket_companion.py status | settime | rec | mark | stop | sync | privacy`. | `pip install bleak` |
| `v10_full` | Full product | OTA firmware updates from the inbox after each sync, task watchdog, Settings screen (version, mic gain, live lux, Wi-Fi). | — |

## Building and flashing

```bash
cd firmware
pio run -e v01_voice_memo -t upload      # build + flash
pio device monitor                        # logs at 115200
make -C test/host                         # host unit tests for the core logic (no board)
```

Hold BOOT while plugging in if the board isn't detected.

## Controls (all versions with a MODE pad)

| Pad | State | Gesture | Action |
| --- | --- | --- | --- |
| REC | Home | Tap | Start recording |
| REC | Recording | Tap | Bookmark (+ photo from v03) |
| REC | Recording | Hold 1.5 s | Stop and save |
| MODE | Any | Tap | Next screen |
| MODE | Home | Hold 2 s | Wi-Fi sync (v05+) |
| MODE | Any | Hold 5 s | Privacy: camera off until toggled back |

## What hasn't been tested on hardware

This code was written without the board in hand. It passes host unit tests (core logic) and a
syntax check of every version against stand-in Arduino headers, but these need checking on the
device, roughly in this order:

1. **PDM mic level**: `mic_gain` in config.json (default ×4). Record speech and check it isn't clipping or too quiet.
2. **E-ink driver class**: `GxEPD2_213_B74` in `display.cpp`. If the image is shifted, try `GxEPD2_213_BN`.
3. **Touch thresholds**: `triggerRatio` 8% in `gesture.h`. Print `pads.rawRec()` if taps are missed or ghosted.
4. **Camera orientation and registers**: `set_vflip` in `camera.cpp`; the standby and exposure-readback registers are best effort for OV3660/OV5640.
5. **USB drive**: TinyUSB MSC with raw SD sectors; test with a computer before an iPhone.
6. **Deep-sleep current** with camera standby: measure with a USB power meter.
