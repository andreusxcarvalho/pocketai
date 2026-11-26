import json
import math
import struct
import wave

from pocketai import adpcm
from pocketai.session import load_session, valid_file_name, valid_session_id


def write_adpcm_wav(path, samples):
    blocks, index = [], 0
    n = len(samples) // adpcm.SAMPLES_PER_BLOCK * adpcm.SAMPLES_PER_BLOCK
    for i in range(0, n, adpcm.SAMPLES_PER_BLOCK):
        b, index = adpcm.encode_block(samples[i:i + adpcm.SAMPLES_PER_BLOCK], index)
        blocks.append(b)
    data = b"".join(blocks)
    # Same 60-byte header layout as firmware/lib/pocket_core/src/wav.cpp
    hdr = b"RIFF" + struct.pack("<I", 52 + len(data)) + b"WAVE"
    hdr += b"fmt " + struct.pack("<IHHIIHHHH", 20, 0x11, 1, 16000, 16000 * 256 // 505, 256, 4, 2, 505)
    hdr += b"fact" + struct.pack("<II", 4, n)
    hdr += b"data" + struct.pack("<I", len(data))
    assert len(hdr) == 60
    path.write_bytes(hdr + data)
    return n


def test_adpcm_roundtrip(tmp_path):
    src = [int(9000 * math.sin(2 * math.pi * 440 * i / 16000)) for i in range(505 * 8)]
    n = write_adpcm_wav(tmp_path / "a.wav", src)
    info = adpcm.read_wav_info(tmp_path / "a.wav")
    assert info["format"] == 0x11 and info["samples_per_block"] == 505 and info["fact_samples"] == n
    assert adpcm.adpcm_to_pcm_wav(tmp_path / "a.wav", tmp_path / "p.wav") == n
    with wave.open(str(tmp_path / "p.wav")) as w:
        out = struct.unpack(f"<{w.getnframes()}h", w.readframes(w.getnframes()))
    err = sum((a - b) ** 2 for a, b in zip(src[100:], out[100:])) / len(out[100:])
    sig = sum(a * a for a in src[100:]) / len(src[100:])
    assert 10 * math.log10(sig / err) > 20


def test_ids():
    assert valid_session_id("2026-11-26_0850") and valid_session_id("boot0007_s002")
    assert not valid_session_id("../x") and not valid_session_id("a/b") and not valid_session_id("")
    assert valid_file_name("img_0001.jpg") and valid_file_name("audio.wav")
    assert not valid_file_name("../audio.wav") and not valid_file_name(".hidden") and not valid_file_name("a..b")


def test_load_session(tmp_path):
    d = tmp_path / "2026-11-26_0850"
    d.mkdir()
    (d / "session.json").write_text(json.dumps({"id": "2026-11-26_0850", "started_at": "2026-11-26T08:50:00",
                                                "duration_ms": 3600000}))
    (d / "marks.json").write_text(json.dumps([{"t_ms": 5000, "photo": "img_0002.jpg"}, {"t_ms": 1000, "photo": "img_0001.jpg"}]))
    for n in ("img_0001.jpg", "img_0002.jpg"):
        (d / n).write_bytes(b"jpg")
    (d / "img_0002.json").write_text(json.dumps({"offset_ms": 5000, "profile": "dim", "gain": 90}))
    s = load_session(d)
    assert s.title == "Thu 26 Nov 2026, 08:50" and s.duration_s == 3600
    assert [p.offset_ms for p in s.photos] == [1000, 5000]  # img_0001 has no sidecar: taken from marks
    assert s.photos[1].profile == "dim" and s.photos[1].gain == 90
