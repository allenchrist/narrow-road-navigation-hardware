#pragma once

// Everything you might need to change is in this file and secrets.h.

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.example.h to include/secrets.h and fill it in"
#endif

// ── Server ──────────────────────────────────────────────────────────────────
// The hosted server. For a server on your laptop, set SERVER_USE_TLS to 0,
// SERVER_HOST to the laptop's address on the hotspot (such as
// "192.168.43.20") and SERVER_PORT to 5000.
#define SERVER_HOST "narrow-road-navigation-ckml.onrender.com"
#define SERVER_PORT 443
#define SERVER_USE_TLS 1
#define VEHICLE_WS_PATH "/vehicle"

// ── Wiring (ESP32-S3 DevKitC-1) ─────────────────────────────────────────────
// GPS module: its TX goes to GPS_RX_PIN, its RX goes to GPS_TX_PIN
#define GPS_RX_PIN 18
#define GPS_TX_PIN 17
#define GPS_BAUD 9600  // u-blox modules start at 9600

// IMU on I2C
#define IMU_SDA_PIN 8
#define IMU_SCL_PIN 9

// Which IMU is fitted: IMU_BNO085 or IMU_BNO055
#define IMU_BNO085
// #define IMU_BNO055
#define BNO085_I2C_ADDRESS 0x4A  // some boards use 0x4B
#define BNO055_I2C_ADDRESS 0x28  // some boards use 0x29

// Added to the IMU's heading, in degrees, so that the reading is 0 when
// the car points north. See "Calibrating the heading" in the README.
#define HEADING_OFFSET_DEG 0.0f
// Set to 1 if the heading goes down when the car turns right
#define HEADING_REVERSED 0

// ── Timing ──────────────────────────────────────────────────────────────────
#define TELEMETRY_INTERVAL_MS 1000   // the app sends about once a second
#define HEADING_MIN_INTERVAL_MS 150  // server accepts every 100 ms at most
#define HEADING_MIN_CHANGE_DEG 3.0f  // send a heading on its own after a turn
#define GPS_MAX_AGE_MS 3000          // older positions aren't sent

// ── Camera link ─────────────────────────────────────────────────────────────
// The camera unit broadcasts road widths on the hotspot to this UDP port
#define CAMERA_UDP_PORT 4210

// ── Finding narrow roads from camera widths ─────────────────────────────────
// The same rules the server's findNarrowRoads.js tool uses
#define NARROW_MAX_WIDTH_M 4.0f       // narrow: 4 m or less between the sides
#define NARROW_MIN_LENGTH_M 25.0f     // along at least 25 m
#define NARROW_MIN_FRACTION 0.70f     // and 70% of the readings on it
#define NARROW_END_GAP_M 15.0f        // ended after 15 m of wider road
#define NARROW_MIN_CONFIDENCE 0.5f    // ignore camera readings below this
#define NARROW_MAX_GPS_ERROR_M 15.0f  // ignore positions worse than this
#define SUGGEST_ROADS 1               // 0 to only log what would be sent
