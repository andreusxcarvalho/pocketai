"""Laptop worker.

  pocketai-worker process ./sessions/2026-11-26_0850 --out ./notes     # one local folder
  pocketai-worker watch --inbox http://pocket-inbox.local:8000 --out ./notes   # pull jobs from the Pi
  pocketai-worker search "fourier transform" --out ./notes
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys
import time
from pathlib import Path

import httpx

from .index import SearchIndex
from .process import Options, process_session


def _opts(a) -> Options:
    return Options(whisper_model=a.whisper, language=a.lang,
                   ollama_model=None if a.no_llm else a.ollama_model, ollama_url=a.ollama_url, ocr=not a.no_ocr)


def cmd_process(a) -> None:
    out = Path(a.out)
    idx = SearchIndex(out / "search.db") if not a.no_index else None
    src = Path(a.session)
    page = process_session(src, out / src.name, _opts(a), index=idx)
    print(page)


def cmd_watch(a) -> None:
    out = Path(a.out)
    idx = SearchIndex(out / "search.db")
    headers = {"Authorization": f"Bearer {a.token}"} if a.token else {}
    client = httpx.Client(base_url=a.inbox, headers=headers, timeout=120)
    print(f"watching {a.inbox} for new sessions…")
    while True:
        try:
            r = client.post("/api/v1/jobs/claim")
        except httpx.HTTPError as e:
            print(f"inbox unreachable: {e}; retrying in {a.interval}s")
            time.sleep(a.interval)
            continue
        if r.status_code == 204:
            if a.once:
                return
            time.sleep(a.interval)
            continue
        r.raise_for_status()
        job = r.json()
        sid, device = job["session"], job["device"]
        local = out / "_downloads" / device / sid
        local.mkdir(parents=True, exist_ok=True)
        try:
            for name in job["files"]:
                with client.stream("GET", f"/api/v1/files/{device}/{sid}/{name}") as resp:
                    resp.raise_for_status()
                    with open(local / name, "wb") as f:
                        for chunk in resp.iter_bytes():
                            f.write(chunk)
            page = process_session(local, out / sid, _opts(a), index=idx)
            client.post(f"/api/v1/jobs/{job['job']}/done", json={"ok": True})
            print(f"done: {page}")
            if not a.keep_downloads:
                shutil.rmtree(local, ignore_errors=True)
        except Exception as e:  # report and keep going
            client.post(f"/api/v1/jobs/{job['job']}/done", json={"ok": False, "error": str(e)[:500]})
            print(f"failed {sid}: {e}", file=sys.stderr)


def cmd_search(a) -> None:
    idx = SearchIndex(Path(a.out) / "search.db")
    for hit in idx.search(a.query, a.limit):
        m, s = divmod(int(hit["start"]), 60)
        print(f"{hit['session']}  {m:02d}:{s:02d}  [{hit['kind']}]  {hit['snippet']}")


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(prog="pocketai-worker")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(p):
        p.add_argument("--out", default="./notes")
        p.add_argument("--whisper", default="small", help="faster-whisper model: tiny/base/small/medium")
        p.add_argument("--lang", default=None, help="force language, e.g. en")
        p.add_argument("--ollama-model", default=os.environ.get("POCKET_OLLAMA_MODEL", "llama3.1:8b"))
        p.add_argument("--ollama-url", default=os.environ.get("POCKET_OLLAMA_URL", "http://localhost:11434"))
        p.add_argument("--no-llm", action="store_true", help="extractive summary only")
        p.add_argument("--no-ocr", action="store_true")

    p = sub.add_parser("process", help="process one local session folder")
    p.add_argument("session")
    p.add_argument("--no-index", action="store_true")
    common(p)
    p.set_defaults(fn=cmd_process)

    w = sub.add_parser("watch", help="pull jobs from the Pi inbox")
    w.add_argument("--inbox", default=os.environ.get("POCKET_INBOX", "http://pocket-inbox.local:8000"))
    w.add_argument("--token", default=os.environ.get("POCKET_TOKEN", ""))
    w.add_argument("--interval", type=int, default=30)
    w.add_argument("--once", action="store_true", help="exit when the queue is empty")
    w.add_argument("--keep-downloads", action="store_true")
    common(w)
    w.set_defaults(fn=cmd_watch)

    s = sub.add_parser("search", help="search all processed sessions")
    s.add_argument("query")
    s.add_argument("--out", default="./notes")
    s.add_argument("--limit", type=int, default=20)
    s.set_defaults(fn=cmd_search)

    a = ap.parse_args(argv)
    a.fn(a)


if __name__ == "__main__":
    main()
