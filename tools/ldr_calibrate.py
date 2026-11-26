#!/usr/bin/env python3
"""Fit the LDR curve (r10, gamma) from a few measurements and print the config.json block.

1. Set "debug_ldr": true in /config.json (v04+). The device prints "ldr <volts> V" every second.
2. Put a phone light-meter app next to the LDR window at 5+ light levels (dark room, lamp,
   room light, window, outdoors) and note volts and lux for each in a CSV:
       volts,lux
       0.21,2
       1.90,35
       ...
3. python3 tools/ldr_calibrate.py readings.csv
"""

import argparse
import csv
import json
import math
import sys


def resistance(v: float, vcc: float, r_fixed: float) -> float:
    return r_fixed * (vcc - v) / v


def fit(rows: list[tuple[float, float]], vcc: float = 3.3, r_fixed: float = 47000.0) -> dict:
    pts = [(math.log(resistance(v, vcc, r_fixed)), math.log(lux)) for v, lux in rows if 0 < v < vcc and lux > 0]
    if len(pts) < 3:
        raise ValueError("need at least 3 usable readings (0 < volts < vcc, lux > 0)")
    n = len(pts)
    mx = sum(x for x, _ in pts) / n
    my = sum(y for _, y in pts) / n
    sxx = sum((x - mx) ** 2 for x, _ in pts)
    sxy = sum((x - mx) * (y - my) for x, y in pts)
    slope = sxy / sxx              # ln(lux) = a + slope * ln(R), slope = -1/gamma
    a = my - slope * mx
    gamma = -1.0 / slope
    r10 = math.exp((a - math.log(10)) * gamma)
    resid = [y - (a + slope * x) for x, y in pts]
    rms = math.sqrt(sum(r * r for r in resid) / n)
    return {"r10": round(r10), "gamma": round(gamma, 3), "r_fixed": r_fixed, "rms_log_error": round(rms, 3)}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv")
    ap.add_argument("--vcc", type=float, default=3.3)
    ap.add_argument("--r-fixed", type=float, default=47000.0)
    a = ap.parse_args()
    with open(a.csv) as f:
        rows = [(float(r["volts"]), float(r["lux"])) for r in csv.DictReader(f)]
    try:
        res = fit(rows, a.vcc, a.r_fixed)
    except ValueError as e:
        sys.exit(str(e))
    print(f"fit: r10 = {res['r10']} ohm, gamma = {res['gamma']}, rms log error = {res['rms_log_error']}")
    if res["rms_log_error"] > 0.4:
        print("warning: readings scatter a lot; check the LDR window and re-measure")
    print('\nAdd to /config.json:\n  "ldr": ' + json.dumps({k: res[k] for k in ("r10", "gamma", "r_fixed")}))


if __name__ == "__main__":
    main()
