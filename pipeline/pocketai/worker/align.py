"""Groups the transcript into readable chunks and places each slide where it was captured."""

from __future__ import annotations

from dataclasses import dataclass, field

from .transcribe import Segment


@dataclass
class Slide:
    name: str          # developed image file name
    offset_s: float
    ocr_text: str = ""
    source: str = "tap"


@dataclass
class Chunk:
    start: float
    end: float
    text: str
    slides: list[Slide] = field(default_factory=list)


def chunk_transcript(segs: list[Segment], target_s: float = 90.0) -> list[Chunk]:
    """Joins segments into ~target_s chunks, breaking only between segments."""
    chunks: list[Chunk] = []
    cur: list[Segment] = []
    for s in segs:
        cur.append(s)
        if cur[-1].end - cur[0].start >= target_s:
            chunks.append(Chunk(cur[0].start, cur[-1].end, " ".join(x.text for x in cur)))
            cur = []
    if cur:
        chunks.append(Chunk(cur[0].start, cur[-1].end, " ".join(x.text for x in cur)))
    return chunks


def place_slides(chunks: list[Chunk], slides: list[Slide]) -> list[Chunk]:
    """Attaches each slide to the chunk being spoken when it was captured. Slides before the
    first speech go to the first chunk; after the last, to the last. With no transcript,
    one empty chunk holds all slides."""
    if not chunks:
        if not slides:
            return []
        end = max(s.offset_s for s in slides)
        return [Chunk(0.0, end, "", sorted(slides, key=lambda s: s.offset_s))]
    for sl in sorted(slides, key=lambda s: s.offset_s):
        target = chunks[-1]
        for c in chunks:
            if sl.offset_s < c.end:
                target = c
                break
        target.slides.append(sl)
    return chunks


def fmt_time(seconds: float) -> str:
    s = int(seconds)
    return f"{s // 3600}:{s // 60 % 60:02d}:{s % 60:02d}" if s >= 3600 else f"{s // 60:02d}:{s % 60:02d}"
