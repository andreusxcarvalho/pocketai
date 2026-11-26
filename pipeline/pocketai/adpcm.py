"""IMA-ADPCM (mono, Microsoft block layout) encode/decode, matching the firmware.

ffmpeg/PyAV (used by faster-whisper) already reads these files, so this module is for
tools and tests: converting to plain PCM WAV and checking firmware output bit-for-bit.
"""

from __future__ import annotations

import struct
import wave
from pathlib import Path

INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]
STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60,
    66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371,
    408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
    1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
    7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
    24623, 27086, 29794, 32767,
]
BLOCK_ALIGN = 256
SAMPLES_PER_BLOCK = (BLOCK_ALIGN - 4) * 2 + 1  # 505


def _clamp16(v: int) -> int:
    return 32767 if v > 32767 else (-32768 if v < -32768 else v)


def _clamp_index(i: int) -> int:
    return 0 if i < 0 else (88 if i > 88 else i)


def decode_block(block: bytes) -> list[int]:
    pred = struct.unpack_from("<h", block, 0)[0]
    index = _clamp_index(block[2])
    out = [pred]
    for b in block[4:]:
        for code in (b & 0x0F, b >> 4):
            step = STEP_TABLE[index]
            diff = step >> 3
            if code & 4:
                diff += step
            if code & 2:
                diff += step >> 1
            if code & 1:
                diff += step >> 2
            pred = _clamp16(pred - diff if code & 8 else pred + diff)
            index = _clamp_index(index + INDEX_TABLE[code])
            out.append(pred)
    return out[:SAMPLES_PER_BLOCK]


def encode_block(samples: list[int], index: int = 0) -> tuple[bytes, int]:
    """Encode exactly SAMPLES_PER_BLOCK samples. Returns (block, next step index)."""
    assert len(samples) == SAMPLES_PER_BLOCK
    pred = samples[0]
    out = bytearray(struct.pack("<hBB", pred, index, 0))
    codes = []
    for s in samples[1:]:
        diff = s - pred
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        step = STEP_TABLE[index]
        vp = step >> 3
        if diff >= step:
            code |= 4
            diff -= step
            vp += step
        step >>= 1
        if diff >= step:
            code |= 2
            diff -= step
            vp += step
        step >>= 1
        if diff >= step:
            code |= 1
            vp += step
        pred = _clamp16(pred - vp if code & 8 else pred + vp)
        index = _clamp_index(index + INDEX_TABLE[code])
        codes.append(code)
    for i in range(0, len(codes), 2):
        out.append(codes[i] | (codes[i + 1] << 4))
    return bytes(out), index


def read_wav_info(path: Path) -> dict:
    """Parses RIFF chunks; returns format, rate, block_align, data offset/size, fact samples."""
    data = Path(path).read_bytes()[:4096]
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a WAV file")
    info: dict = {}
    pos = 12
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack_from("<I", data, pos + 4)[0]
        if cid == b"fmt ":
            fmt, ch, rate, _, align, bits = struct.unpack_from("<HHIIHH", data, pos + 8)
            info.update(format=fmt, channels=ch, rate=rate, block_align=align, bits=bits)
            if fmt == 0x11 and size >= 20:
                info["samples_per_block"] = struct.unpack_from("<H", data, pos + 26)[0]
        elif cid == b"fact":
            info["fact_samples"] = struct.unpack_from("<I", data, pos + 8)[0]
        elif cid == b"data":
            info["data_offset"] = pos + 8
            info["data_size"] = size
            return info
        pos += 8 + size + (size & 1)
    raise ValueError("no data chunk")


def adpcm_to_pcm_wav(src: Path, dst: Path) -> int:
    """Decodes an IMA-ADPCM WAV to 16-bit PCM WAV. Returns sample count. (Pure Python: slow
    for long files; prefer `ffmpeg -i audio.wav -c:a pcm_s16le out.wav` for big ones.)"""
    info = read_wav_info(src)
    if info["format"] == 1:
        Path(dst).write_bytes(Path(src).read_bytes())
        return info["data_size"] // 2
    if info["format"] != 0x11:
        raise ValueError(f"unsupported format 0x{info['format']:x}")
    raw = Path(src).read_bytes()[info["data_offset"]:info["data_offset"] + info["data_size"]]
    align = info["block_align"]
    total = info.get("fact_samples")
    pcm: list[int] = []
    for i in range(0, len(raw) - align + 1, align):
        pcm.extend(decode_block(raw[i:i + align]))
    if total:
        pcm = pcm[:total]
    with wave.open(str(dst), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(info["rate"])
        w.writeframes(struct.pack(f"<{len(pcm)}h", *pcm))
    return len(pcm)
