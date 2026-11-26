"""Reading a recorder session folder (the layout the firmware writes).

/sessions/2026-11-26_0850/
    audio.wav        16 kHz mono, PCM16 or IMA-ADPCM
    marks.json       [{"t_ms": 1503200, "photo": "img_0001.jpg", "source": "tap"}, ...]
    img_0001.jpg     raw camera JPEG
    img_0001.json    lux, profile, exposure, gain, ...
    session.json     id, started_at, clock, firmware, audio_format, duration_ms, status, ...
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

SESSION_ID_RE = re.compile(r"^[A-Za-z0-9_\-]{1,40}$")
FILE_NAME_RE = re.compile(r"^[A-Za-z0-9_\-]{1,40}(\.[A-Za-z0-9]{1,8})?$")


def valid_session_id(s: str) -> bool:
    return bool(SESSION_ID_RE.match(s))


def valid_file_name(s: str) -> bool:
    return bool(FILE_NAME_RE.match(s)) and ".." not in s


@dataclass
class Photo:
    name: str
    path: Path
    offset_ms: int
    meta: dict = field(default_factory=dict)

    @property
    def profile(self) -> str:
        return self.meta.get("profile", "indoor")

    @property
    def gain(self) -> int:
        return int(self.meta.get("gain", 0) or 0)

    @property
    def low_light(self) -> bool:
        return bool(self.meta.get("low_light", False))


@dataclass
class Mark:
    t_ms: int
    photo: str | None
    source: str = "tap"


@dataclass
class Session:
    id: str
    path: Path
    meta: dict
    marks: list[Mark]
    photos: list[Photo]

    @property
    def audio_path(self) -> Path:
        return self.path / "audio.wav"

    @property
    def started_at(self) -> datetime | None:
        s = self.meta.get("started_at")
        if not s:
            return None
        try:
            return datetime.fromisoformat(s)
        except ValueError:
            return None

    @property
    def duration_s(self) -> float:
        return float(self.meta.get("duration_ms", 0)) / 1000.0

    @property
    def title(self) -> str:
        t = self.started_at
        return t.strftime("%a %d %b %Y, %H:%M") if t else f"Session {self.id}"


def _read_json(p: Path, default):
    try:
        return json.loads(p.read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        return default


def load_session(path: Path) -> Session:
    path = Path(path)
    meta = _read_json(path / "session.json", {})
    sid = meta.get("id") or path.name
    marks = [
        Mark(int(m.get("t_ms", 0)), m.get("photo"), m.get("source", "tap"))
        for m in _read_json(path / "marks.json", [])
    ]
    photos: list[Photo] = []
    for jpg in sorted(path.glob("img_*.jpg")):
        side = _read_json(jpg.with_suffix(".json"), {})
        offset = side.get("offset_ms")
        if offset is None:  # fall back to the mark that references this photo
            offset = next((m.t_ms for m in marks if m.photo == jpg.name), 0)
        photos.append(Photo(jpg.name, jpg, int(offset), side))
    photos.sort(key=lambda p: p.offset_ms)
    return Session(sid, path, meta, marks, photos)
