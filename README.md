# Pocket AI recorder

A slim recorder that snaps onto the back of an iPhone, records lectures and meetings to its own
SD card, and photographs the slide with one tap. Recordings sync over Wi-Fi to a Raspberry Pi,
and a laptop turns them into notes (transcript, summary, slides in place) without any cloud.

Hardware: Seeed XIAO ESP32S3 Sense, WeAct 2.13" e-ink, LiPo, GL5528 LDR, MagSafe ring.

## What's here

| Folder | What |
| --- | --- |
| [`firmware/`](firmware/) | ESP32-S3 firmware (PlatformIO, Arduino core 3.x). **10 product versions** from a bare voice memo to the full product: see [docs/versions.md](docs/versions.md). |
| [`firmware/lib/pocket_core/`](firmware/lib/pocket_core/src) | Hardware-free logic (ADPCM, WAV, light profiles, gestures, state machine, slide detection) with host unit tests. |
| [`pipeline/`](pipeline/) | Pi inbox server + laptop worker (faster-whisper, OpenCV photo development, OCR, Ollama summaries, HTML notes, search). |
| [`hardware/case/`](hardware/case/) | Printable cases: **4 variants + desk stand + shirt clip**, STLs and the generator. |
| [`companion/`](companion/) | Bluetooth LE companion CLI (v09+). |
| [`tools/`](tools/) | LDR calibration, fake device for testing the Pi, WAV repair. |
| [`docs/`](docs/) | [Versions](docs/versions.md) · [Wiring](docs/wiring.md) · [Pi + laptop setup](docs/pi-and-laptop-setup.md) · [config example](docs/config.example.json) |

## Quick start

```bash
# 1. Firmware: start with v01, move up as each part works
cd firmware && pio run -e v01_voice_memo -t upload && pio device monitor

# 2. Tests (no hardware needed)
make -C firmware/test/host                       # 21 C++ tests
cd pipeline && pip install -e '.[dev]' && pytest  # 19 Python tests

# 3. Case: print hardware/case/stl/chunky_* first (fits current parts), slim_* later
```

## Status

Written and tested off-device: core logic has unit tests, every firmware version passes a
compile check against stand-in headers, and the Pi → laptop pipeline was run end to end with a
simulated recorder. **Nothing has run on the real board yet.** The list of things to check
first is at the bottom of [docs/versions.md](docs/versions.md).
