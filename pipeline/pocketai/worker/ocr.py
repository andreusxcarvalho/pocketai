"""Slide text via Tesseract (optional). Returns "" when Tesseract isn't installed."""

from __future__ import annotations

from pathlib import Path


def ocr_image(path: Path, lang: str = "eng") -> str:
    try:
        import pytesseract
        from PIL import Image
    except ImportError:
        return ""
    try:
        text = pytesseract.image_to_string(Image.open(path), lang=lang, config="--psm 3")
    except (pytesseract.TesseractNotFoundError, OSError):
        return ""
    lines = [ln.strip() for ln in text.splitlines()]
    return "\n".join(ln for ln in lines if len(ln) > 1)
