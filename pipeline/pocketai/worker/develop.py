"""Photo "development" steered by each photo's sidecar (light profile, sensor gain).

Order: white balance -> denoise -> find and straighten the slide -> contrast -> sharpen.
Strengths come from the light profile the LDR picked on the device, so daylight shots are
left nearly alone and dark, high-gain shots get the heavy treatment.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import cv2
import numpy as np


@dataclass
class DevelopParams:
    wb_strength: float     # 0..1 blend toward gray-world balance
    denoise_h: int         # cv2.fastNlMeansDenoisingColored strength (0 = off)
    clahe_clip: float
    sharpen: float         # unsharp mask amount (0 = off)


PROFILE_PARAMS = {
    "dark": DevelopParams(1.0, 12, 2.0, 0.0),
    "dim": DevelopParams(0.85, 8, 2.0, 0.3),
    "indoor": DevelopParams(0.6, 4, 1.8, 0.5),
    "bright": DevelopParams(0.3, 2, 1.5, 0.5),
    "covered": DevelopParams(1.0, 12, 2.0, 0.0),
}


def params_for(profile: str, gain: int = 0, low_light: bool = False) -> DevelopParams:
    p = PROFILE_PARAMS.get(profile, PROFILE_PARAMS["indoor"])
    h = p.denoise_h
    if gain > 64 or low_light:  # sensor says it pushed gain hard: denoise more
        h = max(h, 10)
    return DevelopParams(p.wb_strength, h, p.clahe_clip, p.sharpen)


def gray_world(img: np.ndarray, strength: float) -> np.ndarray:
    """Scales channels so their means match; removes the green cast of unsettled AWB."""
    f = img.astype(np.float32)
    means = f.reshape(-1, 3).mean(axis=0) + 1e-6
    gains = means.mean() / means
    gains = 1.0 + (gains - 1.0) * strength
    return np.clip(f * gains, 0, 255).astype(np.uint8)


def order_corners(pts: np.ndarray) -> np.ndarray:
    pts = pts.reshape(4, 2).astype(np.float32)
    s = pts.sum(axis=1)
    d = np.diff(pts, axis=1).ravel()
    return np.array([pts[np.argmin(s)], pts[np.argmin(d)], pts[np.argmax(s)], pts[np.argmax(d)]], dtype=np.float32)


def find_slide(img: np.ndarray, min_area_frac: float = 0.15) -> np.ndarray | None:
    """Returns the 4 corners of the largest bright quadrilateral (a projected slide/whiteboard)."""
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    blur = cv2.GaussianBlur(gray, (5, 5), 0)
    _, th = cv2.threshold(blur, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    th = cv2.morphologyEx(th, cv2.MORPH_CLOSE, np.ones((7, 7), np.uint8))
    contours, _ = cv2.findContours(th, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    area_min = min_area_frac * img.shape[0] * img.shape[1]
    best = None
    best_area = 0.0
    for c in contours:
        area = cv2.contourArea(c)
        if area < area_min or area <= best_area:
            continue
        approx = cv2.approxPolyDP(c, 0.02 * cv2.arcLength(c, True), True)
        if len(approx) == 4 and cv2.isContourConvex(approx):
            best, best_area = approx, area
    if best is None:
        return None
    if best_area > 0.97 * img.shape[0] * img.shape[1]:
        return None  # the whole frame: nothing to straighten
    return order_corners(best)


def warp_to_rect(img: np.ndarray, corners: np.ndarray, aspect: float = 16 / 9) -> np.ndarray:
    tl, tr, br, bl = corners
    width = int(max(np.linalg.norm(tr - tl), np.linalg.norm(br - bl)))
    height = int(width / aspect) if aspect else int(max(np.linalg.norm(bl - tl), np.linalg.norm(br - tr)))
    dst = np.array([[0, 0], [width - 1, 0], [width - 1, height - 1], [0, height - 1]], dtype=np.float32)
    m = cv2.getPerspectiveTransform(corners, dst)
    return cv2.warpPerspective(img, m, (width, height))


def clahe(img: np.ndarray, clip: float) -> np.ndarray:
    lab = cv2.cvtColor(img, cv2.COLOR_BGR2LAB)
    l, a, b = cv2.split(lab)
    l = cv2.createCLAHE(clipLimit=clip, tileGridSize=(8, 8)).apply(l)
    return cv2.cvtColor(cv2.merge((l, a, b)), cv2.COLOR_LAB2BGR)


def unsharp(img: np.ndarray, amount: float) -> np.ndarray:
    if amount <= 0:
        return img
    blur = cv2.GaussianBlur(img, (0, 0), 2.0)
    return cv2.addWeighted(img, 1 + amount, blur, -amount, 0)


def develop(img: np.ndarray, profile: str = "indoor", gain: int = 0, low_light: bool = False,
            straighten: bool = True) -> tuple[np.ndarray, bool]:
    """Returns (developed image, whether a slide was found and straightened)."""
    p = params_for(profile, gain, low_light)
    out = gray_world(img, p.wb_strength)
    if p.denoise_h:
        out = cv2.fastNlMeansDenoisingColored(out, None, p.denoise_h, p.denoise_h, 7, 21)
    found = False
    if straighten:
        corners = find_slide(out)
        if corners is not None:
            out = warp_to_rect(out, corners)
            found = True
    out = clahe(out, p.clahe_clip)
    out = unsharp(out, p.sharpen)
    return out, found


def dhash(img: np.ndarray, size: int = 8) -> int:
    """Perceptual difference hash, for dropping near-identical photos of the same slide."""
    g = cv2.resize(cv2.cvtColor(img, cv2.COLOR_BGR2GRAY), (size + 1, size), interpolation=cv2.INTER_AREA)
    g = g.astype(np.float32)
    # A small margin keeps flat areas (walls, slide background) from flipping bits on noise.
    bits = ((g[:, 1:] - g[:, :-1]) > 2.0).flatten()
    return int("".join("1" if b else "0" for b in bits), 2)


def hamming(a: int, b: int) -> int:
    return bin(a ^ b).count("1")


def develop_file(src: Path, dst: Path, meta: dict) -> dict:
    img = cv2.imread(str(src))
    if img is None:
        raise ValueError(f"cannot read {src}")
    out, found = develop(img, meta.get("profile", "indoor"), int(meta.get("gain", 0) or 0),
                         bool(meta.get("low_light", False)))
    cv2.imwrite(str(dst), out, [cv2.IMWRITE_JPEG_QUALITY, 90])
    return {"straightened": found, "hash": dhash(out), "width": out.shape[1], "height": out.shape[0]}
