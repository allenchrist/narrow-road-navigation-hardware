"""Checks the width measurement on synthetic lane pictures.

Run from camera-unit/python:   python -m pytest tests
or without pytest:              python tests/test_width.py
"""

import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)

from scene import render_lane  # noqa: E402
from widthlib import (  # noqa: E402
    Calibration,
    WidthSmoother,
    apply_homography,
    homography_from_camera,
    homography_from_points,
    measure_width,
)

W, H = 320, 240
CAMERA = dict(camera_height_m=1.2, pitch_deg=12.0, hfov_deg=66.0)


def true_calibration():
    return Calibration(homography_from_camera(W, H, 1.2, 12.0, 66.0), W, H)


def test_measures_lanes_of_different_widths():
    cal = true_calibration()
    for road in (2.5, 3.0, 3.5, 4.0, 5.0, 6.0):
        img = render_lane(W, H, road_width_m=road, **CAMERA)
        r = measure_width(img, cal)
        print(f"  {road:.1f} m lane -> {r.width:.2f} m (confidence {r.confidence:.2f})")
        assert r.valid and not r.wide
        assert abs(r.width - road) < 0.15, (road, r.width)


def test_lane_not_centred_on_camera():
    cal = true_calibration()
    img = render_lane(W, H, road_width_m=3.2, road_offset_m=0.6, **CAMERA)
    r = measure_width(img, cal)
    assert r.valid and abs(r.width - 3.2) < 0.15, r.width


def test_wide_road_is_reported_as_wide():
    cal = true_calibration()
    img = render_lane(W, H, road_width_m=14.0, walls=False, **CAMERA)
    r = measure_width(img, cal)
    print(f"  open road -> wide={r.wide}, at least {r.width:.1f} m")
    assert r.valid and r.wide and r.width > 4.0


def test_calibration_from_four_markers():
    # Markers on the ground: a 2 m wide rectangle from 4 to 8 m ahead, as the
    # README describes. Find where the true camera sees them, then calibrate
    # from those pixels as calibrate.py does.
    truth = true_calibration()
    ground = [(-1, 4), (1, 4), (1, 8), (-1, 8)]
    pixels = [apply_homography(truth.to_image, x, y) for x, y in ground]
    # A person clicking is about a pixel out
    pixels = [(u + 0.7, v - 0.6) for u, v in pixels]
    cal = Calibration(homography_from_points(pixels, ground), W, H)
    img = render_lane(W, H, road_width_m=3.0, **CAMERA)
    r = measure_width(img, cal)
    print(f"  marker calibration -> {r.width:.2f} m")
    assert r.valid and abs(r.width - 3.0) < 0.2


def test_rough_calibration_still_close():
    # Calibrated assuming 1.25 m and 11 degrees, but really 1.2 m and 12
    cal = Calibration(homography_from_camera(W, H, 1.25, 11.0, 66.0), W, H)
    img = render_lane(W, H, road_width_m=3.0, **CAMERA)
    r = measure_width(img, cal)
    print(f"  slightly wrong calibration -> {r.width:.2f} m")
    assert r.valid and abs(r.width - 3.0) < 0.5


def test_smoother_ignores_one_bad_frame():
    cal = true_calibration()
    s = WidthSmoother()
    out = None
    for i in range(5):
        img = render_lane(W, H, road_width_m=3.0, seed=i, **CAMERA)
        r = measure_width(img, cal)
        if i == 2:
            r.width = 9.0  # a glitch
        out = s.add(r)
    assert abs(out.width - 3.0) < 0.15


def test_message_format():
    cal = true_calibration()
    r = measure_width(render_lane(W, H, road_width_m=3.0, **CAMERA), cal)
    m = r.message()
    assert m["type"] == "width" and set(m) == {"type", "width", "wide", "distance", "confidence"}


if __name__ == "__main__":
    failed = 0
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            print(name)
            try:
                fn()
            except AssertionError as e:
                failed += 1
                print("  FAIL", e)
    print("All passed" if not failed else f"{failed} failed")
    sys.exit(1 if failed else 0)
