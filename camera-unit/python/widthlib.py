"""Road width from a camera picture.

The same steps as the ESP32-CAM's width_core.cpp, so both give the same
answer for the same picture and calibration:

Calibration gives a homography H that turns a pixel (u, v) into a point on
the flat ground, x metres to the right of the camera and y metres ahead.

For each measuring distance (5, 6 and 7 m ahead by default):
  1. find the pixel row showing the ground at that distance, straight ahead
  2. average a few rows there, and smooth along the row
  3. take the brightness of the road just ahead as the reference
  4. scan left and right until the brightness differs from the road for
     several pixels in a row: that's the wall, kerb or edge of the road
  5. turn both edge pixels into ground points; the distance between them
     is the width
The answer is the median over the distances. If a scan reaches the edge of
the picture, the road is wider than the camera sees ("wide").
"""

from __future__ import annotations

import json
import math
from collections import deque
from dataclasses import dataclass, field

import numpy as np


# ── Homographies ────────────────────────────────────────────────────────────


def apply_homography(h: np.ndarray, u: float, v: float) -> tuple[float, float] | None:
    w = h[2, 0] * u + h[2, 1] * v + h[2, 2]
    if abs(w) < 1e-12:
        return None
    return (
        (h[0, 0] * u + h[0, 1] * v + h[0, 2]) / w,
        (h[1, 0] * u + h[1, 1] * v + h[1, 2]) / w,
    )


def homography_from_camera(
    width: int, height: int, height_m: float, pitch_deg: float, hfov_deg: float
) -> np.ndarray:
    """Image-to-ground homography for a level camera at height_m, tilted down
    by pitch_deg, with a horizontal field of view of hfov_deg. A starting point
    before calibrating with markers."""
    f = (width / 2) / math.tan(math.radians(hfov_deg) / 2)
    cx, cy = width / 2, height / 2
    s, c = math.sin(math.radians(pitch_deg)), math.cos(math.radians(pitch_deg))
    k = np.array([[f, 0, cx], [0, f, cy], [0, 0, 1]])
    # Ground (x right, y ahead, 1) to camera (right, down, forward)
    m = np.array([[1, 0, 0], [0, -s, height_m * c], [0, c, height_m * s]])
    ground_to_image = k @ m
    return np.linalg.inv(ground_to_image)


def homography_from_points(image_points, ground_points) -> np.ndarray:
    """Image-to-ground homography from four (or more) matching points."""
    import cv2

    src = np.asarray(image_points, dtype=np.float64)
    dst = np.asarray(ground_points, dtype=np.float64)
    if len(src) == 4:
        h = cv2.getPerspectiveTransform(src.astype(np.float32), dst.astype(np.float32))
    else:
        h, _ = cv2.findHomography(src, dst)
    return h.astype(np.float64)


@dataclass
class Calibration:
    to_ground: np.ndarray
    width: int
    height: int

    @property
    def to_image(self) -> np.ndarray:
        return np.linalg.inv(self.to_ground)

    def save(self, path: str) -> None:
        with open(path, "w") as f:
            json.dump(
                {
                    "width": self.width,
                    "height": self.height,
                    "imageToGround": self.to_ground.tolist(),
                },
                f,
                indent=2,
            )

    @staticmethod
    def load(path: str) -> "Calibration":
        with open(path) as f:
            data = json.load(f)
        return Calibration(np.array(data["imageToGround"], dtype=np.float64), data["width"], data["height"])

    def cpp_define(self) -> str:
        values = ", ".join(f"{v:.9g}" for v in self.to_ground.flatten())
        return f"#define CAM_IMAGE_TO_GROUND {{{values}}}"


# ── Measuring ───────────────────────────────────────────────────────────────


@dataclass
class WidthSettings:
    distances: tuple = (5.0, 6.0, 7.0)
    row_band: int = 2
    smooth: int = 2
    reference_half: int = 8
    min_run: int = 4
    min_contrast: float = 22.0
    mad_factor: float = 3.0
    narrow_max_m: float = 4.0


@dataclass
class RowResult:
    v: int
    centre: int
    left: int
    right: int
    open_left: bool
    open_right: bool
    width: float


@dataclass
class WidthResult:
    valid: bool = False
    wide: bool = False
    width: float = 0.0
    confidence: float = 0.0
    distance: float = 0.0
    rows: list = field(default_factory=list)

    def message(self) -> dict:
        """The packet the main unit and screen listen for."""
        return {
            "type": "width",
            "width": round(self.width, 2),
            "wide": self.wide,
            "distance": self.distance,
            "confidence": round(self.confidence, 2),
        }


def _median(values):
    values = sorted(values)
    n = len(values)
    return values[n // 2] if n % 2 else 0.5 * (values[n // 2 - 1] + values[n // 2])


def measure_width(gray: np.ndarray, cal: Calibration, s: WidthSettings = WidthSettings()) -> WidthResult:
    height, width = gray.shape[:2]
    to_image = cal.to_image
    result = WidthResult(distance=s.distances[len(s.distances) // 2])
    closed, opened, tried = [], [], 0

    for d in s.distances:
        p = apply_homography(to_image, 0.0, d)
        if p is None:
            continue
        centre, v = int(round(p[0])), int(round(p[1]))
        if v - s.row_band < 0 or v + s.row_band >= height:
            continue
        if centre - s.reference_half < 1 or centre + s.reference_half >= width - 1:
            continue
        tried += 1

        # 1-2: average a band of rows, then smooth along the row (edges use
        # fewer pixels, the same as the C++ version)
        profile = gray[v - s.row_band : v + s.row_band + 1].astype(np.float64).mean(axis=0)
        csum = np.concatenate([[0.0], np.cumsum(profile)])
        idx = np.arange(width)
        a = np.maximum(0, idx - s.smooth)
        b = np.minimum(width - 1, idx + s.smooth)
        smoothed = (csum[b + 1] - csum[a]) / (b - a + 1)

        # 3: the road just ahead, and how much it varies by itself
        window = smoothed[centre - s.reference_half : centre + s.reference_half + 1]
        reference = _median(list(window))
        mad = _median(list(np.abs(window - reference)))
        threshold = max(s.min_contrast, s.mad_factor * mad)
        differs = np.abs(smoothed - reference) > threshold

        # 4: scan outwards for the sides
        def scan(step):
            run = 0
            u = centre + step * (s.reference_half + 1)
            while 0 <= u < width:
                if differs[u]:
                    run += 1
                    if run >= s.min_run:
                        # The differing pixel nearest the road, moved out by
                        # half the smoothing, which spreads the side inwards
                        edge = u - step * (s.min_run - 1) + step * (s.smooth // 2)
                        return min(width - 1, max(0, edge)), False
                else:
                    run = 0
                u += step
            return (0 if step < 0 else width - 1), True

        left, open_left = scan(-1)
        right, open_right = scan(+1)

        # 5: both edges onto the ground
        gl = apply_homography(cal.to_ground, left, v)
        gr = apply_homography(cal.to_ground, right, v)
        if gl is None or gr is None:
            continue
        w = math.hypot(gr[0] - gl[0], gr[1] - gl[1])
        result.rows.append(RowResult(v, centre, left, right, open_left, open_right, w))
        (opened if (open_left or open_right) else closed).append(w)

    if tried == 0:
        return result
    needed = (tried + 1) // 2
    if len(closed) >= needed:
        result.valid, result.width = True, _median(closed)
        result.confidence = len(closed) / tried
    elif len(opened) >= needed:
        at_least = _median(opened)
        if at_least > s.narrow_max_m:
            result.valid, result.wide, result.width = True, True, at_least
            result.confidence = len(opened) / tried
    return result


class WidthSmoother:
    """Median of the last few results, so one bad frame doesn't jump the reading."""

    def __init__(self, size: int = 5):
        self.history: deque = deque(maxlen=size)

    def add(self, r: WidthResult) -> WidthResult:
        if not r.valid:
            return r
        self.history.append(r)
        wide = sum(1 for h in self.history if h.wide)
        widths = [h.width for h in self.history if not h.wide]
        out = WidthResult(True, False, r.width, sum(h.confidence for h in self.history) / len(self.history), r.distance, r.rows)
        if wide * 2 > len(self.history) or not widths:
            out.wide = True
        else:
            out.width = _median(widths)
        return out


def draw_overlay(image: np.ndarray, result: WidthResult) -> np.ndarray:
    """The picture with the measured rows and sides marked."""
    import cv2

    out = image.copy() if image.ndim == 3 else cv2.cvtColor(image, cv2.COLOR_GRAY2BGR)
    for row in result.rows:
        cv2.line(out, (row.left, row.v), (row.right, row.v), (255, 200, 0), 1)
        for u, opened in ((row.left, row.open_left), (row.right, row.open_right)):
            colour = (0, 165, 255) if opened else (0, 0, 255)
            cv2.circle(out, (u, row.v), 4, colour, -1)
        cv2.circle(out, (row.centre, row.v), 3, (0, 255, 0), -1)
    if result.valid:
        text = f">{result.width:.1f} m (wide)" if result.wide else f"{result.width:.2f} m"
        colour = (0, 200, 255) if (not result.wide and result.width <= 4.0) else (0, 220, 0)
    else:
        text, colour = "no reading", (180, 180, 180)
    scale = max(0.5, out.shape[1] / 640)
    cv2.putText(out, text, (10, int(30 * scale)), cv2.FONT_HERSHEY_SIMPLEX, scale, (0, 0, 0), 4)
    cv2.putText(out, text, (10, int(30 * scale)), cv2.FONT_HERSHEY_SIMPLEX, scale, colour, 2)
    return out
