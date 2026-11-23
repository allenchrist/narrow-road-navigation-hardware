"""Opening any of the cameras the camera unit supports.

  0, 1, ...                          a USB webcam (0 is the first one)
  http://PHONE:8080/video            a phone running the IP Webcam app (Android)
  http://PHONE:4747/video            a phone running DroidCam (Android or iPhone)
  http://CAMERA:81/stream            an ESP32-CAM in stream mode
  http://CAMERA/frame.bmp            one picture from an ESP32-CAM in measure mode
  road.mp4 / road.jpg                a recorded video or a photo, for testing
"""

from __future__ import annotations

import time
import urllib.request

import cv2
import numpy as np

IMAGE_EXTENSIONS = (".jpg", ".jpeg", ".png", ".bmp")


def is_still_image(source: str) -> bool:
    return source.lower().split("?")[0].endswith(IMAGE_EXTENSIONS)


def read_still(source: str) -> np.ndarray:
    """A single picture from a file or a URL."""
    if source.startswith(("http://", "https://")):
        with urllib.request.urlopen(source, timeout=10) as response:
            data = np.frombuffer(response.read(), dtype=np.uint8)
        img = cv2.imdecode(data, cv2.IMREAD_COLOR)
    else:
        img = cv2.imread(source, cv2.IMREAD_COLOR)
    if img is None:
        raise SystemExit(f"Couldn't read a picture from {source}")
    return img


class FrameSource:
    """Frames from a webcam, stream or video file, reconnecting if a stream drops."""

    def __init__(self, source: str):
        self.source = source
        self.capture = None
        self.still = is_still_image(source)
        self.open()

    def open(self) -> None:
        if self.still:
            return
        target = int(self.source) if self.source.isdigit() else self.source
        self.capture = cv2.VideoCapture(target)
        if isinstance(target, int):
            # Webcams: ask for a modest size; it's scaled to the calibration anyway
            self.capture.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
            self.capture.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        # Streams: keep only the newest frame so readings aren't delayed
        self.capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        if not self.capture.isOpened():
            print(f"Can't open {self.source} yet; will keep trying")

    def read(self) -> np.ndarray | None:
        if self.still:
            return read_still(self.source)
        ok, frame = self.capture.read() if self.capture else (False, None)
        if ok:
            return frame
        if self.is_file():
            return None  # end of the video
        print("Lost the camera; reconnecting...")
        if self.capture:
            self.capture.release()
        time.sleep(1.0)
        self.open()
        return None

    def is_file(self) -> bool:
        return not self.source.isdigit() and "://" not in self.source

    def close(self) -> None:
        if self.capture:
            self.capture.release()
