"""Renders one session's notes as a self-contained HTML page (plus Markdown)."""

from __future__ import annotations

import html
from pathlib import Path

from .align import Chunk, fmt_time
from .summarize import Summary

CSS = """
body{font:16px/1.6 -apple-system,Segoe UI,Roboto,sans-serif;max-width:860px;margin:2rem auto;padding:0 1rem;color:#222}
h1{font-size:1.6rem;margin-bottom:.2rem} .meta{color:#777;margin-top:0}
.summary{background:#f6f5f2;border-radius:10px;padding:1rem 1.25rem}
.chunk{border-top:1px solid #eee;padding:.75rem 0} .t{color:#999;font-size:.85rem;font-variant-numeric:tabular-nums}
figure{margin:.75rem 0} figure img{max-width:100%;border-radius:6px;border:1px solid #ddd}
figcaption{color:#888;font-size:.85rem} pre.ocr{white-space:pre-wrap;color:#555;font-size:.85rem;background:#fafafa;padding:.5rem;border-radius:6px}
@media (prefers-color-scheme:dark){body{background:#1a1a1a;color:#ddd}.summary{background:#262626}.chunk{border-color:#333}pre.ocr{background:#222;color:#aaa}}
"""


def render_html(title: str, meta_line: str, summary: Summary, chunks: list[Chunk], img_dir: str = "slides") -> str:
    e = html.escape
    parts = [f"<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
             f"<title>{e(title)}</title><style>{CSS}</style></head><body>",
             f"<h1>{e(title)}</h1><p class='meta'>{e(meta_line)}</p>",
             "<section class='summary'>"]
    if summary.summary:
        parts.append(f"<p>{e(summary.summary)}</p>")
    if summary.key_points:
        parts.append("<h3>Key points</h3><ul>" + "".join(f"<li>{e(p)}</li>" for p in summary.key_points) + "</ul>")
    terms = [t for t in summary.terms if t.get("term")]
    if terms:
        parts.append("<h3>Terms</h3><ul>" + "".join(
            f"<li><b>{e(str(t['term']))}</b>{(' – ' + e(str(t.get('meaning')))) if t.get('meaning') else ''}</li>"
            for t in terms) + "</ul>")
    if summary.questions:
        parts.append("<h3>Questions to revise</h3><ol>" + "".join(f"<li>{e(q)}</li>" for q in summary.questions) + "</ol>")
    parts.append(f"<p class='meta'>Summary: {e(summary.model)}</p></section><h2>Transcript</h2>")
    for c in chunks:
        parts.append(f"<div class='chunk'><span class='t'>{fmt_time(c.start)}</span>")
        if c.text:
            parts.append(f"<p>{e(c.text)}</p>")
        for s in c.slides:
            parts.append(f"<figure><img src='{e(img_dir)}/{e(s.name)}' loading='lazy'>"
                         f"<figcaption>{fmt_time(s.offset_s)} · {e(s.source)}</figcaption>")
            if s.ocr_text:
                parts.append(f"<pre class='ocr'>{e(s.ocr_text)}</pre>")
            parts.append("</figure>")
        parts.append("</div>")
    parts.append("</body></html>")
    return "".join(parts)


def render_markdown(title: str, meta_line: str, summary: Summary, chunks: list[Chunk], img_dir: str = "slides") -> str:
    out = [f"# {title}", "", f"_{meta_line}_", ""]
    if summary.summary:
        out += [summary.summary, ""]
    if summary.key_points:
        out += ["## Key points", *[f"- {p}" for p in summary.key_points], ""]
    if summary.questions:
        out += ["## Questions to revise", *[f"{i}. {q}" for i, q in enumerate(summary.questions, 1)], ""]
    out += ["## Transcript", ""]
    for c in chunks:
        out.append(f"**{fmt_time(c.start)}** {c.text}".rstrip())
        for s in c.slides:
            out.append(f"\n![{fmt_time(s.offset_s)}]({img_dir}/{s.name})")
        out.append("")
    return "\n".join(out)


def write_notes(out_dir: Path, title: str, meta_line: str, summary: Summary, chunks: list[Chunk]) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "notes.md").write_text(render_markdown(title, meta_line, summary, chunks))
    p = out_dir / "index.html"
    p.write_text(render_html(title, meta_line, summary, chunks))
    return p
