"""Speech to text with faster-whisper (runs on the laptop; imported lazily).

faster-whisper decodes audio through PyAV/ffmpeg, which reads both the PCM and the
IMA-ADPCM WAVs the recorder writes, so no conversion step is needed.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class Segment:
    start: float
    end: float
    text: str
    words: list[tuple[float, float, str]] = field(default_factory=list)


def transcribe(audio: Path, model_size: str = "small", language: str | None = None,
               device: str = "auto", compute_type: str = "int8") -> list[Segment]:
    try:
        from faster_whisper import WhisperModel
    except ImportError as e:  # pragma: no cover - depends on optional extra
        raise RuntimeError("install the worker extra: pip install 'pocketai[worker]'") from e
    model = WhisperModel(model_size, device=device, compute_type=compute_type)
    segments, _info = model.transcribe(
        str(audio), language=language, vad_filter=True, word_timestamps=True, beam_size=5
    )
    out = []
    for s in segments:
        words = [(w.start, w.end, w.word) for w in (s.words or [])]
        out.append(Segment(s.start, s.end, s.text.strip(), words))
    return out


def segments_to_json(segs: list[Segment]) -> list[dict]:
    return [{"start": s.start, "end": s.end, "text": s.text} for s in segs]


def segments_from_json(data: list[dict]) -> list[Segment]:
    return [Segment(float(d["start"]), float(d["end"]), d["text"]) for d in data]
