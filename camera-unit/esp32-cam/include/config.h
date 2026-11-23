#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.example.h to include/secrets.h and fill it in"
#endif

// ── What the camera does ────────────────────────────────────────────────────
// CAMERA_MODE_MEASURE: measures the road width on the ESP32-CAM itself and
//   broadcasts it. Also serves http://<camera address>/ with the latest
//   reading and /frame.bmp, the picture to calibrate with.
// CAMERA_MODE_STREAM: only sends video, at http://<camera address>:81/stream,
//   for the Python program (width.py) on a laptop to measure. More accurate
//   lighting handling, but needs the laptop in the car.
#define CAMERA_MODE_MEASURE
// #define CAMERA_MODE_STREAM

// ── Calibration ─────────────────────────────────────────────────────────────
// Turns a pixel of the 320x240 picture into a point on the ground. Replace
// this line with the one calibrate.py prints (see the README).
// This starting value assumes the camera 1.2 m up, tilted 12 degrees down,
// with the usual ESP32-CAM lens (about 55 degrees across).
#define CAM_IMAGE_TO_GROUND {0.00325354407, -1.00791852e-18, -0.520567051, 0, -0.000676449848, 1.05932158, -0, 0.0026520386, -0.14498489}

// Metres ahead to measure at; the camera must see the ground there
#define MEASURE_DISTANCES {5.0f, 6.0f, 7.0f}

// Mounted upside down? Turn the picture the right way up
#define CAMERA_FLIP_VERTICAL 0
#define CAMERA_MIRROR 0

// ── Sending ─────────────────────────────────────────────────────────────────
#define CAMERA_UDP_PORT 4210
#define MEASURE_INTERVAL_MS 200  // about five readings a second
#define NARROW_MAX_WIDTH_M 4.0f
