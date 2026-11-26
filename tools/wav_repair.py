#!/usr/bin/env python3
"""Fix the header of a recording cut off by a power loss (same logic as the firmware's
repair-on-boot), and optionally convert IMA-ADPCM to plain 16-bit PCM.

  python3 tools/wav_repair.py /Volumes/POCKET/sessions/2026-11-26_0850/audio.wav
  python3 tools/wav_repair.py audio.wav --to-pcm audio_pcm.wav
"""

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "pipeline"))
from pocketai.adpcm import adpcm_to_pcm_wav, read_wav_info  # noqa: E402


def repair(path: Path) -> str:
    info = read_wav_info(path)
    size = path.stat().st_size
    data = size - info["data_offset"]
    align = info["block_align"]
    data -= data % align  # drop a torn final block
    buf = bytearray(path.read_bytes()[:info["data_offset"]])
    struct.pack_into("<I", buf, 4, info["data_offset"] - 8 + data)
    struct.pack_into("<I", buf, info["data_offset"] - 4, data)
    if info["format"] == 0x11:
        samples = data // align * info["samples_per_block"]
        fact = buf.find(b"fact")
        if fact >= 0:
            struct.pack_into("<I", buf, fact + 8, samples)
        secs = samples / info["rate"]
    else:
        secs = data / 2 / info["rate"]
    with open(path, "r+b") as f:
        f.write(buf)
        f.truncate(info["data_offset"] + data)
    return f"{path.name}: {data} bytes of audio, {secs / 60:.1f} min"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("wav", type=Path)
    ap.add_argument("--to-pcm", type=Path)
    a = ap.parse_args()
    print(repair(a.wav))
    if a.to_pcm:
        n = adpcm_to_pcm_wav(a.wav, a.to_pcm)
        print(f"wrote {a.to_pcm} ({n} samples)")


if __name__ == "__main__":
    main()
