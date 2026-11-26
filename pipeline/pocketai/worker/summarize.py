"""Summaries from a local LLM via Ollama, with an offline extractive fallback.

Nothing leaves the laptop: Ollama runs locally (default http://localhost:11434).
"""

from __future__ import annotations

import json
import re
from collections import Counter
from dataclasses import dataclass, field

import httpx

PROMPT = """You are writing study notes from a lecture or meeting transcript.
Slide text captured during the session is included in [SLIDE] blocks.
Return JSON with keys:
  "summary": one paragraph, under 120 words,
  "key_points": list of 5-8 short bullet strings,
  "terms": list of {{"term": str, "meaning": str}} for important terms,
  "questions": list of 3-5 revision questions.
Only use information in the transcript and slides.

TRANSCRIPT:
{text}
"""


@dataclass
class Summary:
    summary: str
    key_points: list[str] = field(default_factory=list)
    terms: list[dict] = field(default_factory=list)
    questions: list[str] = field(default_factory=list)
    model: str = "extractive"


STOP = set("""a an the and or but if then so to of in on at for with by from as is are was were be
been being it this that these those there here we you they he she i me my our your their its
not no yes do does did have has had will would can could should may might just also very
really like um uh okay right yeah so now what which who whom how when where why all any
some more most much many such than too into over about up down out""".split())


def _sentences(text: str) -> list[str]:
    return [s.strip() for s in re.split(r"(?<=[.!?])\s+", text) if len(s.strip()) > 20]


def extractive(text: str, n: int = 5) -> Summary:
    """Frequency-based fallback: picks the n most content-heavy sentences, in order."""
    sents = _sentences(text)
    words = [w for w in re.findall(r"[a-zA-Z][a-zA-Z\-]+", text.lower()) if w not in STOP]
    freq = Counter(words)
    scored = []
    for i, s in enumerate(sents):
        toks = [w for w in re.findall(r"[a-zA-Z][a-zA-Z\-]+", s.lower()) if w not in STOP]
        if toks:
            scored.append((sum(freq[t] for t in toks) / len(toks) ** 0.5, i, s))
    top = sorted(sorted(scored, reverse=True)[:n], key=lambda x: x[1])
    points = [s for _, _, s in top]
    terms = [{"term": w, "meaning": ""} for w, _ in freq.most_common(8)]
    return Summary(" ".join(points[:2]), points, terms, [], "extractive")


def ollama(text: str, model: str = "llama3.1:8b", url: str = "http://localhost:11434",
           timeout: float = 600.0) -> Summary:
    r = httpx.post(f"{url}/api/generate", timeout=timeout, json={
        "model": model, "prompt": PROMPT.format(text=text[:60000]), "format": "json", "stream": False,
        "options": {"temperature": 0.2},
    })
    r.raise_for_status()
    data = json.loads(r.json()["response"])
    return Summary(
        str(data.get("summary", "")),
        [str(x) for x in data.get("key_points", [])],
        [t for t in data.get("terms", []) if isinstance(t, dict)],
        [str(x) for x in data.get("questions", [])],
        model,
    )


def summarize(text: str, model: str | None = "llama3.1:8b", url: str = "http://localhost:11434") -> Summary:
    if not text.strip():
        return Summary("No speech was transcribed.")
    if model:
        try:
            return ollama(text, model, url)
        except (httpx.HTTPError, json.JSONDecodeError, KeyError) as e:
            print(f"ollama unavailable ({e.__class__.__name__}); using extractive summary")
    return extractive(text)
