"""Calibrates the camera, so pixels can be turned into metres on the ground.

Do this once the camera is fixed in its final place on the car, and again
if it moves.

On flat ground in front of the car, put four markers (stones, tape or
chalk crosses) at the corners of a rectangle centred on the car's line:
by default 2 m wide, from 4 m to 8 m ahead of the camera. Measure along
the ground from directly below the camera.

            far-left  x - - - - x  far-right       8 m ahead
                      |          |
                      |          |
           near-left  x - - - - x  near-right      4 m ahead
                          2 m
                       [  car  ]

Then, with the same camera and picture size you'll measure with:
  python calibrate.py --source 0                               USB webcam
  python calibrate.py --source http://192.168.43.1:8080/video  phone camera
  python calibrate.py --source http://CAMERA-ADDRESS/frame.bmp ESP32-CAM
and click the markers in order: near-left, near-right, far-right, far-left.
Press Enter to save, r to start again, Esc to cancel.

Without a window, give the pixel positions yourself:
  python calibrate.py --source photo.jpg --points "84,190 236,190 205,140 115,140"

Or start from measurements of the camera instead of markers (rougher):
  python calibrate.py --size 320x240 --camera-height 1.2 --pitch 12 --hfov 55

It writes calibration.json for width.py, and prints a line to paste into the
ESP32-CAM's include/config.h.
"""

from __future__ import annotations

import argparse

import cv2

from sources import FrameSource
from widthlib import (
    Calibration,
    WidthSettings,
    draw_overlay,
    homography_from_camera,
    homography_from_points,
    measure_width,
)

ORDER = ["near-left", "near-right", "far-right", "far-left"]


def ground_corners(width_m: float, near_m: float, far_m: float):
    half = width_m / 2
    return [(-half, near_m), (half, near_m), (half, far_m), (-half, far_m)]


def grab_frame(source: str, size):
    src = FrameSource(source)
    frame = None
    for _ in range(30):  # webcams need a few frames to settle their exposure
        f = src.read()
        if f is not None:
            frame = f
        if src.still:
            break
    src.close()
    if frame is None:
        raise SystemExit(f"No picture from {source}")
    if size:
        frame = cv2.resize(frame, size, interpolation=cv2.INTER_AREA)
    return frame


def click_points(frame):
    """Shows the picture twice as large; returns four clicked (u, v) pixels."""
    scale = 2
    points = []
    window = "Click: " + ", ".join(ORDER)

    def on_mouse(event, x, y, *_):
        if event == cv2.EVENT_LBUTTONDOWN and len(points) < 4:
            points.append((x / scale, y / scale))

    cv2.namedWindow(window)
    cv2.setMouseCallback(window, on_mouse)
    while True:
        shown = cv2.resize(frame, None, fx=scale, fy=scale, interpolation=cv2.INTER_NEAREST)
        for i, (u, v) in enumerate(points):
            cv2.drawMarker(shown, (int(u * scale), int(v * scale)), (0, 0, 255), cv2.MARKER_CROSS, 24, 2)
            cv2.putText(shown, ORDER[i], (int(u * scale) + 8, int(v * scale) - 8),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 255), 2)
        hint = f"Click the {ORDER[len(points)]} marker" if len(points) < 4 else "Enter: save   r: redo"
        cv2.putText(shown, hint, (10, 25), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)
        cv2.imshow(window, shown)
        key = cv2.waitKey(20) & 0xFF
        if key == 27:
            raise SystemExit("Cancelled")
        if key == ord("r"):
            points.clear()
        if key in (13, 10) and len(points) == 4:
            break
    cv2.destroyWindow(window)
    return points


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--source", help="camera, stream URL or picture to calibrate from")
    p.add_argument("--size", default="320x240",
                   help="picture size to calibrate for; 320x240 matches the ESP32-CAM")
    p.add_argument("--rect", default="2,4,8", help="markers: width, near and far distance in metres")
    p.add_argument("--points", help='marker pixels "u,v u,v u,v u,v" instead of clicking')
    p.add_argument("--camera-height", type=float, help="metres; with --pitch and --hfov, no markers")
    p.add_argument("--pitch", type=float, help="degrees the camera tilts down")
    p.add_argument("--hfov", type=float, default=55.0, help="camera's horizontal field of view, degrees")
    p.add_argument("--out", default="calibration.json")
    args = p.parse_args()

    w, h = (int(x) for x in args.size.lower().split("x"))
    frame = None

    if args.camera_height is not None:
        if args.pitch is None:
            raise SystemExit("--camera-height needs --pitch too")
        cal = Calibration(homography_from_camera(w, h, args.camera_height, args.pitch, args.hfov), w, h)
    else:
        width_m, near_m, far_m = (float(x) for x in args.rect.split(","))
        if args.points:
            pixels = [tuple(float(c) for c in pt.split(",")) for pt in args.points.split()]
            if len(pixels) != 4:
                raise SystemExit("--points needs four u,v pairs")
            if args.source:
                frame = grab_frame(args.source, (w, h))
        else:
            if not args.source:
                raise SystemExit("Give --source, --points, or --camera-height (see --help)")
            frame = grab_frame(args.source, (w, h))
            pixels = click_points(frame)
        cal = Calibration(homography_from_points(pixels, ground_corners(width_m, near_m, far_m)), w, h)

    cal.save(args.out)
    print(f"Saved {args.out}")
    print("\nFor the ESP32-CAM, replace the CAM_IMAGE_TO_GROUND line in include/config.h with:\n")
    print(cal.cpp_define())

    # A first check on the calibration picture
    if frame is not None:
        r = measure_width(cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY), cal, WidthSettings())
        if r.valid:
            print(f"\nOn this picture: {'more than ' if r.wide else ''}{r.width:.2f} m wide")
        cv2.imwrite("calibration-check.png", draw_overlay(frame, r))
        print("Saved calibration-check.png with the measurement marked")


if __name__ == "__main__":
    main()
