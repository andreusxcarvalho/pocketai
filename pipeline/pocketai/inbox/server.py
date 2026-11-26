"""Inbox server for the Raspberry Pi: receives sessions from the recorder over Wi-Fi,
verifies checksums, queues processing jobs for the laptop worker, and serves firmware.

Run:  POCKET_TOKEN=secret POCKET_DATA=/mnt/usb/pocket pocketai-inbox --port 8000
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import os
import re
import shutil
import sqlite3
import tempfile
import threading
import time
from pathlib import Path

from fastapi import Depends, FastAPI, Header, HTTPException, Request, Response
from fastapi.responses import FileResponse, JSONResponse

from ..session import valid_file_name, valid_session_id

SCHEMA = """
CREATE TABLE IF NOT EXISTS sessions (
  device TEXT NOT NULL, id TEXT NOT NULL, complete INTEGER NOT NULL DEFAULT 0,
  created REAL NOT NULL, completed REAL, PRIMARY KEY (device, id));
CREATE TABLE IF NOT EXISTS files (
  device TEXT NOT NULL, id TEXT NOT NULL, name TEXT NOT NULL, sha256 TEXT NOT NULL,
  size INTEGER NOT NULL, PRIMARY KEY (device, id, name));
CREATE TABLE IF NOT EXISTS jobs (
  job INTEGER PRIMARY KEY AUTOINCREMENT, device TEXT NOT NULL, id TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'queued', claimed REAL, finished REAL, error TEXT);
"""

CLAIM_TIMEOUT_S = 6 * 3600  # a claimed job is re-queued if the worker vanished


class Inbox:
    def __init__(self, root: Path, token: str | None):
        self.root = Path(root)
        self.token = token or ""
        (self.root / "sessions").mkdir(parents=True, exist_ok=True)
        (self.root / "firmware").mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()
        self.db = sqlite3.connect(self.root / "inbox.db", check_same_thread=False)
        self.db.executescript(SCHEMA)
        self.db.commit()

    def q(self, sql: str, args: tuple = ()) -> list[tuple]:
        with self._lock:
            cur = self.db.execute(sql, args)
            rows = cur.fetchall()
            self.db.commit()
            return rows

    def session_dir(self, device: str, sid: str) -> Path:
        return self.root / "sessions" / device / sid


def create_app(root: Path | str | None = None, token: str | None = None) -> FastAPI:
    root = Path(root or os.environ.get("POCKET_DATA", "./data"))
    token = token if token is not None else os.environ.get("POCKET_TOKEN", "")
    inbox = Inbox(root, token)
    app = FastAPI(title="Pocket inbox", version="0.1.0")
    app.state.inbox = inbox

    def auth(authorization: str = Header(default="")) -> None:
        if not inbox.token:
            return
        expected = f"Bearer {inbox.token}"
        if not hmac.compare_digest(authorization.encode(), expected.encode()):
            raise HTTPException(401, "bad token")

    def device_name(x_device: str = Header(default="pocket")) -> str:
        if not valid_session_id(x_device):
            raise HTTPException(400, "bad device name")
        return x_device

    def check_ids(sid: str, name: str | None = None) -> None:
        if not valid_session_id(sid):
            raise HTTPException(400, "bad session id")
        if name is not None and not valid_file_name(name):
            raise HTTPException(400, "bad file name")

    @app.get("/api/v1/status")
    def status() -> dict:
        queued = inbox.q("SELECT COUNT(*) FROM jobs WHERE status='queued'")[0][0]
        total = inbox.q("SELECT COUNT(*) FROM sessions WHERE complete=1")[0][0]
        return {"ok": True, "sessions": total, "queued_jobs": queued}

    @app.get("/api/v1/sessions", dependencies=[Depends(auth)])
    def list_sessions(device: str = Depends(device_name)) -> dict:
        rows = inbox.q("SELECT id FROM sessions WHERE device=? AND complete=1 ORDER BY id", (device,))
        return {"complete": [r[0] for r in rows]}

    @app.put("/api/v1/sessions/{sid}/files/{name}", dependencies=[Depends(auth)])
    async def put_file(sid: str, name: str, request: Request,
                       device: str = Depends(device_name),
                       x_sha256: str = Header(default="")) -> dict:
        check_ids(sid, name)
        if len(x_sha256) != 64:
            raise HTTPException(400, "X-Sha256 header required")
        dest_dir = inbox.session_dir(device, sid)
        dest_dir.mkdir(parents=True, exist_ok=True)
        h = hashlib.sha256()
        size = 0
        fd, tmp = tempfile.mkstemp(dir=dest_dir, prefix=".up-")
        try:
            with os.fdopen(fd, "wb") as f:
                async for chunk in request.stream():
                    h.update(chunk)
                    f.write(chunk)
                    size += len(chunk)
            digest = h.hexdigest()
            if not hmac.compare_digest(digest, x_sha256.lower()):
                raise HTTPException(409, f"checksum mismatch: got {digest}")
            os.replace(tmp, dest_dir / name)
        finally:
            if os.path.exists(tmp):
                os.unlink(tmp)
        inbox.q("INSERT OR IGNORE INTO sessions(device,id,created) VALUES (?,?,?)", (device, sid, time.time()))
        inbox.q("INSERT OR REPLACE INTO files(device,id,name,sha256,size) VALUES (?,?,?,?,?)",
                (device, sid, name, digest, size))
        return {"ok": True, "size": size}

    @app.post("/api/v1/sessions/{sid}/complete", dependencies=[Depends(auth)])
    async def complete(sid: str, request: Request, device: str = Depends(device_name)) -> dict:
        check_ids(sid)
        body = await request.json()
        wanted: dict = body.get("files", {})
        have = {n: s for n, s in inbox.q("SELECT name, sha256 FROM files WHERE device=? AND id=?", (device, sid))}
        missing = [n for n, s in wanted.items() if have.get(n) != s]
        if missing or not wanted:
            raise HTTPException(409, f"missing or mismatched: {missing or 'no files'}")
        already = inbox.q("SELECT complete FROM sessions WHERE device=? AND id=?", (device, sid))
        inbox.q("UPDATE sessions SET complete=1, completed=? WHERE device=? AND id=?", (time.time(), device, sid))
        if not (already and already[0][0]):
            inbox.q("INSERT INTO jobs(device,id) VALUES (?,?)", (device, sid))
        return {"ok": True}

    # ---- worker API ----
    @app.post("/api/v1/jobs/claim", dependencies=[Depends(auth)])
    def claim() -> Response:
        stale = time.time() - CLAIM_TIMEOUT_S
        inbox.q("UPDATE jobs SET status='queued' WHERE status='claimed' AND claimed < ?", (stale,))
        rows = inbox.q("SELECT job, device, id FROM jobs WHERE status='queued' ORDER BY job LIMIT 1")
        if not rows:
            return Response(status_code=204)
        job, device, sid = rows[0]
        inbox.q("UPDATE jobs SET status='claimed', claimed=? WHERE job=?", (time.time(), job))
        files = [r[0] for r in inbox.q("SELECT name FROM files WHERE device=? AND id=? ORDER BY name", (device, sid))]
        return JSONResponse({"job": job, "device": device, "session": sid, "files": files})

    @app.get("/api/v1/files/{device}/{sid}/{name}", dependencies=[Depends(auth)])
    def get_file(device: str, sid: str, name: str) -> FileResponse:
        if not valid_session_id(device):
            raise HTTPException(400, "bad device")
        check_ids(sid, name)
        p = inbox.session_dir(device, sid) / name
        if not p.is_file():
            raise HTTPException(404, "no such file")
        return FileResponse(p)

    @app.post("/api/v1/jobs/{job}/done", dependencies=[Depends(auth)])
    async def done(job: int, request: Request) -> dict:
        body = await request.json()
        ok = bool(body.get("ok"))
        inbox.q("UPDATE jobs SET status=?, finished=?, error=? WHERE job=?",
                ("done" if ok else "failed", time.time(), body.get("error"), job))
        return {"ok": True}

    # ---- firmware (OTA for v10) ----
    @app.get("/api/v1/firmware/latest", dependencies=[Depends(auth)])
    def fw_latest(variant: str, current: str = "") -> Response:
        if not valid_session_id(variant):
            raise HTTPException(400, "bad variant")
        meta = inbox.root / "firmware" / variant / "latest.json"
        if not meta.is_file():
            return Response(status_code=204)
        info = json.loads(meta.read_text())
        if info.get("version") == current:
            return Response(status_code=204)
        info["url"] = f"/api/v1/firmware/{variant}/{info['version']}.bin"
        return JSONResponse(info)

    @app.get("/api/v1/firmware/{variant}/{filename}", dependencies=[Depends(auth)])
    def fw_file(variant: str, filename: str) -> FileResponse:
        if not valid_session_id(variant) or not re.fullmatch(r"\d{1,4}\.\d{1,4}\.\d{1,4}\.bin", filename):
            raise HTTPException(400, "bad name")
        p = inbox.root / "firmware" / variant / filename
        if not p.is_file():
            raise HTTPException(404, "no such firmware")
        return FileResponse(p, media_type="application/octet-stream")

    return app


def publish_firmware(root: Path, variant: str, version: str, binary: Path) -> Path:
    """Copies a built firmware.bin into the inbox so v10 devices pick it up on next sync."""
    d = Path(root) / "firmware" / variant
    d.mkdir(parents=True, exist_ok=True)
    dest = d / f"{version}.bin"
    shutil.copyfile(binary, dest)
    sha = hashlib.sha256(dest.read_bytes()).hexdigest()
    (d / "latest.json").write_text(json.dumps({"version": version, "sha256": sha}))
    return dest


def main() -> None:
    ap = argparse.ArgumentParser(description="Pocket inbox server (run on the Pi)")
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--data", default=os.environ.get("POCKET_DATA", "./data"))
    ap.add_argument("--publish-firmware", nargs=3, metavar=("VARIANT", "VERSION", "BIN"))
    a = ap.parse_args()
    if a.publish_firmware:
        v, ver, b = a.publish_firmware
        print(publish_firmware(Path(a.data), v, ver, Path(b)))
        return
    import uvicorn

    if not os.environ.get("POCKET_TOKEN"):
        print("warning: POCKET_TOKEN not set; the inbox accepts uploads from anyone on the network")
    uvicorn.run(create_app(a.data), host=a.host, port=a.port)


if __name__ == "__main__":
    main()
