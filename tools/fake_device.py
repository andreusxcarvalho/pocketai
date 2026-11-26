#!/usr/bin/env python3
"""Pretend to be a recorder: build a session (tone audio + slide photo) and sync it to an
inbox exactly like the firmware does. Use it to test the Pi + laptop setup before the
hardware works.

  python3 tools/fake_device.py --inbox http://pocket-inbox.local:8000 --token secret
"""

import argparse
import hashlib
import json
import math
import struct
import sys
from datetime import datetime
from pathlib import Path

import httpx


def tone_wav(seconds: float = 5.0, rate: int = 16000) -> bytes:
    n = int(seconds * rate)
    pcm = struct.pack(f"<{n}h", *(int(6000 * math.sin(2 * math.pi * 330 * i / rate)) for i in range(n)))
    hdr = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    return hdr + b"data" + struct.pack("<I", len(pcm)) + pcm


def slide_jpg() -> bytes:
    try:
        import cv2
        import numpy as np
    except ImportError:
        return b""
    img = np.full((600, 800, 3), 40, np.uint8)
    cv2.rectangle(img, (100, 100), (700, 480), (235, 235, 235), -1)
    cv2.putText(img, "Hello from fake_device", (130, 300), cv2.FONT_HERSHEY_SIMPLEX, 1.2, (20, 20, 20), 2)
    return cv2.imencode(".jpg", img)[1].tobytes()


def build_session() -> tuple[str, dict[str, bytes]]:
    now = datetime.now()
    sid = now.strftime("%Y-%m-%d_%H%M")
    files = {
        "audio.wav": tone_wav(),
        "marks.json": json.dumps([{"t_ms": 2000, "photo": "img_0001.jpg", "source": "tap"}]).encode(),
        "session.json": json.dumps({"id": sid, "started_at": now.isoformat(timespec="seconds"), "clock": "ntp",
                                    "firmware": "fake-device", "audio_format": "pcm16", "sample_rate": 16000,
                                    "duration_ms": 5000, "status": "complete", "marks": 1, "photos": 1}).encode(),
    }
    jpg = slide_jpg()
    if jpg:
        files["img_0001.jpg"] = jpg
        files["img_0001.json"] = json.dumps({"session": sid, "offset_ms": 2000, "lux": 320, "profile": "indoor",
                                             "gain": 10, "source": "tap"}).encode()
    return sid, files


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--inbox", default="http://localhost:8000")
    ap.add_argument("--token", default="")
    ap.add_argument("--device", default="pocket")
    ap.add_argument("--save", help="also write the session folder here")
    a = ap.parse_args()
    sid, files = build_session()
    if a.save:
        d = Path(a.save) / sid
        d.mkdir(parents=True, exist_ok=True)
        for n, b in files.items():
            (d / n).write_bytes(b)
    headers = {"X-Device": a.device}
    if a.token:
        headers["Authorization"] = f"Bearer {a.token}"
    with httpx.Client(base_url=a.inbox, headers=headers, timeout=30) as c:
        manifest = {}
        for name, data in files.items():
            sha = hashlib.sha256(data).hexdigest()
            r = c.put(f"/api/v1/sessions/{sid}/files/{name}", content=data, headers={"X-Sha256": sha})
            print(f"PUT {name}: {r.status_code}")
            if r.status_code != 200:
                sys.exit(r.text)
            manifest[name] = sha
        r = c.post(f"/api/v1/sessions/{sid}/complete", json={"files": manifest})
        print(f"complete {sid}: {r.status_code} {r.text}")


if __name__ == "__main__":
    main()
