import json

import cv2

from pocketai.worker.align import Slide, chunk_transcript, place_slides
from pocketai.worker.index import SearchIndex
from pocketai.worker.process import Options, process_session
from pocketai.worker.summarize import extractive, summarize
from pocketai.worker.transcribe import Segment
from tests.test_develop import slide_scene

TEXT = [
    "Today we introduce the Fourier series and why periodic signals matter.",
    "A Fourier series writes a periodic signal as a sum of sines and cosines.",
    "The coefficients measure how much of each frequency the signal contains.",
    "Next we look at convergence and the Gibbs phenomenon near discontinuities.",
    "Finally the Fourier transform extends the idea to signals that are not periodic.",
]


def fake_segments():
    return [Segment(i * 40.0, i * 40.0 + 38.0, t) for i, t in enumerate(TEXT)]


def test_chunk_and_place():
    chunks = chunk_transcript(fake_segments(), target_s=70)
    assert len(chunks) == 3 and chunks[0].start == 0 and "Fourier" in chunks[0].text
    placed = place_slides(chunks, [Slide("a.jpg", 10), Slide("b.jpg", 125), Slide("c.jpg", 999)])
    assert [s.name for s in placed[0].slides] == ["a.jpg"]
    assert [s.name for s in placed[1].slides] == ["b.jpg"]
    assert [s.name for s in placed[-1].slides] == ["c.jpg"]
    only = place_slides([], [Slide("x.jpg", 5)])
    assert len(only) == 1 and only[0].slides[0].name == "x.jpg"


def test_extractive_summary_offline():
    s = extractive(" ".join(TEXT), n=3)
    assert len(s.key_points) == 3 and s.model == "extractive"
    assert any("Fourier" in p for p in s.key_points)
    # unreachable Ollama falls back instead of failing
    s2 = summarize(" ".join(TEXT), model="nope", url="http://127.0.0.1:9")
    assert s2.model == "extractive"


def test_process_session_end_to_end(tmp_path):
    d = tmp_path / "sessions" / "2026-11-26_0850"
    d.mkdir(parents=True)
    (d / "session.json").write_text(json.dumps({"id": d.name, "started_at": "2026-11-26T08:50:00",
                                                "duration_ms": 200000, "firmware": "v10-full 0.1.0"}))
    (d / "audio.wav").write_bytes(b"RIFF")  # not decoded: the fake transcriber is used
    img = slide_scene()
    cv2.imwrite(str(d / "img_0001.jpg"), img)
    cv2.imwrite(str(d / "img_0002.jpg"), img)  # duplicate: should be dropped
    (d / "img_0001.json").write_text(json.dumps({"offset_ms": 41000, "profile": "dim", "gain": 80}))
    (d / "img_0002.json").write_text(json.dumps({"offset_ms": 90000, "profile": "dim", "gain": 80}))
    (d / "marks.json").write_text("[]")

    idx = SearchIndex(tmp_path / "search.db")
    out = tmp_path / "notes" / d.name
    page = process_session(d, out, Options(ollama_model=None, ocr=False),
                           transcriber=lambda p: fake_segments(), index=idx)
    html = page.read_text()
    assert "Thu 26 Nov 2026, 08:50" in html and "slides/img_0001.jpg" in html
    assert "img_0002.jpg" not in html  # deduplicated
    assert (out / "slides" / "img_0001.jpg").exists() and (out / "notes.md").exists()
    hits = idx.search("Gibbs")
    assert hits and hits[0]["session"] == d.name and hits[0]["start"] == 120.0
    # second run reuses the cached transcript (transcriber would raise)
    def boom(p):
        raise AssertionError("should use cache")
    process_session(d, out, Options(ollama_model=None, ocr=False), transcriber=boom)
