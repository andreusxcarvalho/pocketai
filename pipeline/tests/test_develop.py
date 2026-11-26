import cv2
import numpy as np

from pocketai.worker import develop as dev


def slide_scene(skew: bool = True) -> np.ndarray:
    """Dark room with a bright, text-covered slide, optionally seen at an angle."""
    img = np.full((600, 800, 3), 30, np.uint8)
    slide = np.full((360, 640, 3), 235, np.uint8)
    for y in range(60, 320, 40):
        cv2.putText(slide, "Fourier series", (40, y), cv2.FONT_HERSHEY_SIMPLEX, 1.1, (20, 20, 20), 2)
    src = np.float32([[0, 0], [640, 0], [640, 360], [0, 360]])
    dst = np.float32([[120, 90], [700, 140], [680, 500], [100, 470]]) if skew else np.float32(
        [[80, 120], [720, 120], [720, 480], [80, 480]])
    m = cv2.getPerspectiveTransform(src, dst)
    warped = cv2.warpPerspective(slide, m, (800, 600))
    mask = cv2.warpPerspective(np.full((360, 640), 255, np.uint8), m, (800, 600))
    img[mask > 0] = warped[mask > 0]
    return img


def test_gray_world_removes_green_cast():
    img = slide_scene()
    green = img.astype(np.float32) * np.array([0.8, 1.25, 0.8])  # BGR: boost green
    green = np.clip(green, 0, 255).astype(np.uint8)
    fixed = dev.gray_world(green, 1.0)
    m = fixed.reshape(-1, 3).mean(axis=0)
    assert m.max() - m.min() < 3.0
    m0 = green.reshape(-1, 3).mean(axis=0)
    assert m0.max() - m0.min() > 10.0


def test_find_and_straighten_slide():
    img = slide_scene(skew=True)
    corners = dev.find_slide(img)
    assert corners is not None
    tl, tr, br, bl = corners
    assert tl[0] < tr[0] and bl[0] < br[0] and tl[1] < bl[1]
    out = dev.warp_to_rect(img, corners)
    assert abs(out.shape[1] / out.shape[0] - 16 / 9) < 0.02
    assert out.mean() > 150  # mostly slide, little dark room left


def test_develop_profiles_and_full_frame():
    out, found = dev.develop(slide_scene(), "dim", gain=100)
    assert found
    plain = np.full((300, 400, 3), 200, np.uint8)
    out2, found2 = dev.develop(plain, "bright")
    assert not found2 and out2.shape == plain.shape
    assert dev.params_for("dark").denoise_h > dev.params_for("bright").denoise_h
    assert dev.params_for("indoor", gain=200).denoise_h >= 10


def test_dhash_dedupes_near_duplicates():
    a = slide_scene(skew=False)
    noisy = np.clip(a.astype(np.int16) + np.random.default_rng(0).integers(-6, 6, a.shape), 0, 255).astype(np.uint8)
    other = np.full_like(a, 30)
    cv2.circle(other, (400, 300), 200, (230, 230, 230), -1)
    assert dev.hamming(dev.dhash(a), dev.dhash(noisy)) <= 6
    assert dev.hamming(dev.dhash(a), dev.dhash(other)) > 6
