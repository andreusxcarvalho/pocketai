import hashlib

import pytest
from fastapi.testclient import TestClient

from pocketai.inbox.server import create_app, publish_firmware

TOKEN = "s3cret"
H = {"Authorization": f"Bearer {TOKEN}", "X-Device": "pocket"}


@pytest.fixture
def client(tmp_path):
    return TestClient(create_app(tmp_path, TOKEN))


def put(client, sid, name, data, sha=None, headers=H):
    return client.put(f"/api/v1/sessions/{sid}/files/{name}", content=data,
                      headers={**headers, "X-Sha256": sha or hashlib.sha256(data).hexdigest()})


def test_auth_required(client):
    assert client.get("/api/v1/sessions", headers={"X-Device": "pocket"}).status_code == 401
    assert client.get("/api/v1/sessions", headers=H).json() == {"complete": []}


def test_upload_complete_and_job_flow(client, tmp_path):
    audio, meta = b"RIFF....fake", b'{"id":"2026-11-26_0850"}'
    assert put(client, "2026-11-26_0850", "audio.wav", audio).status_code == 200
    assert put(client, "2026-11-26_0850", "session.json", meta).status_code == 200
    manifest = {"files": {"audio.wav": hashlib.sha256(audio).hexdigest(),
                          "session.json": hashlib.sha256(meta).hexdigest()}}
    assert client.post("/api/v1/sessions/2026-11-26_0850/complete", json=manifest, headers=H).status_code == 200
    assert client.get("/api/v1/sessions", headers=H).json() == {"complete": ["2026-11-26_0850"]}
    assert (tmp_path / "sessions/pocket/2026-11-26_0850/audio.wav").read_bytes() == audio

    # completing twice doesn't queue a second job
    client.post("/api/v1/sessions/2026-11-26_0850/complete", json=manifest, headers=H)
    job = client.post("/api/v1/jobs/claim", headers=H).json()
    assert job["session"] == "2026-11-26_0850" and set(job["files"]) == {"audio.wav", "session.json"}
    assert client.post("/api/v1/jobs/claim", headers=H).status_code == 204
    got = client.get(f"/api/v1/files/pocket/2026-11-26_0850/audio.wav", headers=H)
    assert got.content == audio
    assert client.post(f"/api/v1/jobs/{job['job']}/done", json={"ok": True}, headers=H).status_code == 200
    assert client.get("/api/v1/status").json()["sessions"] == 1


def test_checksum_mismatch_rejected_and_not_stored(client, tmp_path):
    r = put(client, "s1", "audio.wav", b"data", sha="0" * 64)
    assert r.status_code == 409
    assert not (tmp_path / "sessions/pocket/s1/audio.wav").exists()
    assert list((tmp_path / "sessions/pocket/s1").glob(".up-*")) == []


def test_complete_requires_matching_files(client):
    put(client, "s2", "audio.wav", b"abc")
    r = client.post("/api/v1/sessions/s2/complete", json={"files": {"audio.wav": "f" * 64}}, headers=H)
    assert r.status_code == 409
    assert client.get("/api/v1/sessions", headers=H).json() == {"complete": []}


@pytest.mark.parametrize("sid,name", [("..", "audio.wav"), ("ok", "..%2Fx"), ("a/b", "x.wav"), ("ok", ".hidden")])
def test_path_traversal_rejected(client, sid, name):
    r = put(client, sid, name, b"x")
    assert r.status_code in (400, 404)


def test_firmware_latest(client, tmp_path):
    assert client.get("/api/v1/firmware/latest?variant=v10-full&current=0.1.0", headers=H).status_code == 204
    binf = tmp_path / "fw.bin"
    binf.write_bytes(b"\x00firmware")
    publish_firmware(tmp_path, "v10-full", "0.2.0", binf)
    r = client.get("/api/v1/firmware/latest?variant=v10-full&current=0.1.0", headers=H)
    assert r.status_code == 200 and r.json()["url"] == "/api/v1/firmware/v10-full/0.2.0.bin"
    assert client.get(r.json()["url"], headers=H).content == b"\x00firmware"
    assert client.get("/api/v1/firmware/latest?variant=v10-full&current=0.2.0", headers=H).status_code == 204
