"""Turns one session folder into notes: transcribe, develop photos, OCR, align, summarise,
render, index. Each step caches its output so a re-run only redoes what's missing."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from ..session import load_session
from . import develop as dev
from .align import Slide, chunk_transcript, fmt_time, place_slides
from .index import SearchIndex
from .ocr import ocr_image
from .render import write_notes
from .summarize import summarize
from .transcribe import Segment, segments_from_json, segments_to_json


@dataclass
class Options:
    whisper_model: str = "small"
    language: str | None = None
    ollama_model: str | None = "llama3.1:8b"
    ollama_url: str = "http://localhost:11434"
    dedupe_distance: int = 6     # dHash bits; lower = stricter "same slide"
    ocr: bool = True


Transcriber = Callable[[Path], list[Segment]]


def process_session(session_dir: Path, out_dir: Path, opts: Options = Options(),
                    transcriber: Transcriber | None = None, index: SearchIndex | None = None) -> Path:
    s = load_session(session_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    work = out_dir / "work"
    work.mkdir(exist_ok=True)

    # 1. transcript (cached)
    tpath = work / "transcript.json"
    if tpath.exists():
        segs = segments_from_json(json.loads(tpath.read_text()))
    elif s.audio_path.exists():
        if transcriber is None:
            from .transcribe import transcribe

            def transcriber(p: Path) -> list[Segment]:
                return transcribe(p, opts.whisper_model, opts.language)
        segs = transcriber(s.audio_path)
        tpath.write_text(json.dumps(segments_to_json(segs)))
    else:
        segs = []

    # 2. photos: develop, drop near-duplicates, OCR
    slides_dir = out_dir / "slides"
    slides_dir.mkdir(exist_ok=True)
    slides: list[Slide] = []
    kept_hashes: list[int] = []
    for p in s.photos:
        dst = slides_dir / p.name
        info = dev.develop_file(p.path, dst, p.meta)
        if any(dev.hamming(info["hash"], h) <= opts.dedupe_distance for h in kept_hashes):
            dst.unlink(missing_ok=True)
            continue
        kept_hashes.append(info["hash"])
        text = ocr_image(dst) if opts.ocr else ""
        slides.append(Slide(p.name, p.offset_ms / 1000.0, text, p.meta.get("source", "tap")))

    # 3. align + summarise
    chunks = place_slides(chunk_transcript(segs), slides)
    full_text = " ".join(c.text for c in chunks)
    slide_text = "\n".join(f"[SLIDE {fmt_time(sl.offset_s)}]\n{sl.ocr_text}" for sl in slides if sl.ocr_text)
    spath = work / "summary.json"
    summary = summarize(full_text + ("\n\n" + slide_text if slide_text else ""), opts.ollama_model, opts.ollama_url)
    spath.write_text(json.dumps(summary.__dict__))

    # 4. render + index
    meta_line = f"{fmt_time(s.duration_s)} recorded · {len(slides)} slides · {s.meta.get('firmware', '')}".strip(" ·")
    page = write_notes(out_dir, s.title, meta_line, summary, chunks)
    if index is not None:
        rows = [(c.start, "speech", c.text) for c in chunks]
        rows += [(sl.offset_s, "slide", sl.ocr_text) for sl in slides]
        index.replace_session(s.id, rows)
    return page
