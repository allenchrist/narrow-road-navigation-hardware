"""Measures the road's width from a camera and broadcasts it on the hotspot.

Runs on a laptop or Raspberry Pi in the car, joined to the same hotspot as
the main unit and screen. They hear the readings exactly as they would from
the ESP32-CAM.

Examples (calibrate first, see calibrate.py):
  python width.py --source 0                               USB webcam
  python width.py --source http://192.168.43.1:8080/video  phone, IP Webcam app
  python width.py --source http://192.168.43.1:4747/video  phone, DroidCam
  python width.py --source http://192.168.43.50:81/stream  ESP32-CAM, stream mode
  python width.py --source lane.jpg                        a photo, to try it out

Keys in the window: q quits, s saves the current picture.
"""

from __future__ import annotations

import argparse
import json
import socket
import time

import cv2

from sources import FrameSource
from widthlib import Calibration, WidthSettings, WidthSmoother, draw_overlay, measure_width


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--source", required=True, help="0 for a USB webcam, a stream URL, or a file")
    p.add_argument("--calibration", default="calibration.json", help="from calibrate.py")
    p.add_argument("--port", type=int, default=4210, help="UDP port the car units listen on")
    p.add_argument("--target", default="<broadcast>", help="send to one address instead of everyone")
    p.add_argument("--rate", type=float, default=5.0, help="readings a second")
    p.add_argument("--distances", default="5,6,7", help="metres ahead to measure at")
    p.add_argument("--no-window", action="store_true", help="don't show the picture")
    args = p.parse_args()

    try:
        cal = Calibration.load(args.calibration)
    except FileNotFoundError:
        raise SystemExit(f"No {args.calibration}: run calibrate.py first")

    settings = WidthSettings(distances=tuple(float(d) for d in args.distances.split(",")))
    smoother = WidthSmoother()
    source = FrameSource(args.source)

    sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sender.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    interval = 1.0 / args.rate
    last_print = 0.0
    warned_aspect = False
    print(f"Measuring; sending to {args.target}:{args.port}. Press q in the window to stop.")

    while True:
        started = time.time()
        frame = source.read()
        if frame is None:
            if source.is_file():
                break
            continue

        # The calibration is for one picture size: scale every frame to it
        h, w = frame.shape[:2]
        if (w, h) != (cal.width, cal.height):
            if not warned_aspect and abs(w / h - cal.width / cal.height) > 0.02:
                print(f"Warning: camera gives {w}x{h} but calibration is {cal.width}x{cal.height}")
                warned_aspect = True
            frame = cv2.resize(frame, (cal.width, cal.height), interpolation=cv2.INTER_AREA)
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

        raw = measure_width(gray, cal, settings)
        reading = smoother.add(raw)

        if reading.valid:
            packet = json.dumps(reading.message()).encode()
            try:
                sender.sendto(packet, (args.target, args.port))
            except OSError as e:
                print(f"Couldn't send: {e}")

        if time.time() - last_print > 1.0:
            last_print = time.time()
            if reading.valid:
                kind = "wide" if reading.wide else ("NARROW" if reading.width <= 4.0 else "")
                print(f"{'>' if reading.wide else ''}{reading.width:.2f} m  "
                      f"confidence {reading.confidence:.2f}  {kind}")
            else:
                print("no reading: is the road 5-7 m ahead in view?")

        if not args.no_window:
            shown = draw_overlay(frame, raw)
            shown = cv2.resize(shown, (cal.width * 2, cal.height * 2), interpolation=cv2.INTER_NEAREST)
            cv2.imshow("Road width (q to quit)", shown)
            key = cv2.waitKey(1 if not source.still else 0) & 0xFF
            if key == ord("q"):
                break
            if key == ord("s"):
                name = time.strftime("road-%Y%m%d-%H%M%S.png")
                cv2.imwrite(name, frame)
                print(f"Saved {name}")
        if source.still and args.no_window:
            break

        time.sleep(max(0.0, interval - (time.time() - started)))

    source.close()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
