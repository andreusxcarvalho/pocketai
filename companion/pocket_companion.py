#!/usr/bin/env python3
"""Bluetooth LE companion for v09+ (laptop, Raspberry Pi, or any machine with BLE).

  pip install bleak
  python3 companion/pocket_companion.py status          # live status
  python3 companion/pocket_companion.py settime         # set the recorder clock from this machine
  python3 companion/pocket_companion.py rec|mark|stop|sync|privacy
"""

import argparse
import asyncio
import json
import time

try:
    from bleak import BleakClient, BleakScanner
except ImportError:  # pragma: no cover
    raise SystemExit("pip install bleak")

SERVICE = "7a1e0001-5c6f-4b8e-9f1a-0c0ffee00001"
STATUS = "7a1e0002-5c6f-4b8e-9f1a-0c0ffee00001"
CONTROL = "7a1e0003-5c6f-4b8e-9f1a-0c0ffee00001"


async def find(name: str | None, timeout: float):
    print("scanning…")
    devices = await BleakScanner.discover(timeout=timeout, service_uuids=[SERVICE])
    for d in devices:
        if name is None or d.name == name:
            return d
    raise SystemExit("no Pocket recorder found (is it awake? touch REC to wake it)")


def show(raw: bytes) -> None:
    try:
        s = json.loads(raw.decode())
    except ValueError:
        print(raw)
        return
    rec = f"  rec {s.get('rec_s', 0) // 60}:{s.get('rec_s', 0) % 60:02d}" if s.get("state") == "recording" else ""
    flags = " ".join(k for k in ("privacy", "docked") if s.get(k))
    print(f"[{s.get('fw')}] {s.get('state')}{rec}  marks {s.get('marks')}  photos {s.get('photos')}  "
          f"battery {s.get('battery')}%  clock {'ok' if s.get('clock') else 'not set'} {flags}")


async def run(cmd: str, name: str | None, timeout: float) -> None:
    dev = await find(name, timeout)
    async with BleakClient(dev) as c:
        if cmd == "status":
            show(await c.read_gatt_char(STATUS))
            await c.start_notify(STATUS, lambda _, data: show(bytes(data)))
            print("listening (Ctrl+C to stop)")
            while True:
                await asyncio.sleep(1)
        payload = f"time:{int(time.time())}" if cmd == "settime" else cmd
        await c.write_gatt_char(CONTROL, payload.encode(), response=True)
        await asyncio.sleep(0.5)
        show(await c.read_gatt_char(STATUS))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["status", "settime", "rec", "mark", "stop", "sync", "privacy"])
    ap.add_argument("--name", help="device_name from config.json (default: first found)")
    ap.add_argument("--timeout", type=float, default=8.0)
    a = ap.parse_args()
    try:
        asyncio.run(run(a.cmd, a.name, a.timeout))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
