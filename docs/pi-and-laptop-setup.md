# Pi inbox and laptop worker

```
recorder ──Wi-Fi──▶ Raspberry Pi inbox ──jobs──▶ laptop worker ──▶ notes/<session>/index.html
          (hold MODE)   (always on, stores files)    (transcribe, photos, summary)
```

## 1. Raspberry Pi (inbox)

```bash
sudo apt install python3-venv
git clone https://github.com/andreusxcarvalho/pocketai && cd pocketai/pipeline
python3 -m venv .venv && . .venv/bin/activate
pip install -e .
export POCKET_TOKEN=$(openssl rand -hex 16)   # put the same value in the recorder's config.json
export POCKET_DATA=/home/pi/pocket-data        # or a USB drive
pocketai-inbox --port 8000
```

Give the Pi a stable name (`sudo hostnamectl set-hostname pocket-inbox`, then it answers at
`pocket-inbox.local`). To start it at boot, wrap the command in a systemd service.

Test without the recorder:

```bash
python3 tools/fake_device.py --inbox http://pocket-inbox.local:8000 --token $POCKET_TOKEN
curl http://pocket-inbox.local:8000/api/v1/status
```

## 2. Recorder

Copy [`config.example.json`](config.example.json) to the SD card as `/config.json` and fill in
Wi-Fi, `server` and `token`. Hold MODE for 2 s on the Home screen to sync.

## 3. Laptop (worker)

```bash
cd pocketai/pipeline
python3 -m venv .venv && . .venv/bin/activate
pip install -e '.[worker]'                    # faster-whisper + pytesseract
brew install tesseract ollama                 # or your OS's packages
ollama pull llama3.1:8b
pocketai-worker watch --inbox http://pocket-inbox.local:8000 --token <token> --out ~/PocketNotes
```

Or process a session copied off the SD card (USB drive mode on v08+):

```bash
pocketai-worker process /Volumes/POCKET/sessions/2026-11-26_0850 --out ~/PocketNotes
pocketai-worker search "fourier transform" --out ~/PocketNotes
```

Options: `--whisper tiny|base|small|medium`, `--lang en`, `--no-llm` (offline extractive summary),
`--no-ocr`.

## 4. Firmware updates over the air (v10)

```bash
cd firmware && pio run -e v10_full
pocketai-inbox --data $POCKET_DATA --publish-firmware v10-full 0.2.0 .pio/build/v10_full/firmware.bin
```

Bump `POCKET_FW_SEMVER` in `pocket_features.h` first. The recorder installs it after its next sync.
