#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.example.h to include/secrets.h and fill it in"
#endif

// ── Server ──────────────────────────────────────────────────────────────────
// For a server on your laptop: SERVER_USE_TLS 0, SERVER_HOST the laptop's
// address on the hotspot, SERVER_PORT 5000.
#define SERVER_HOST "narrow-road-navigation-ckml.onrender.com"
#define SERVER_PORT 443
#define SERVER_USE_TLS 1

// ── Clock ───────────────────────────────────────────────────────────────────
#define TIME_ZONE_OFFSET_S (5 * 3600 + 30 * 60)  // India, UTC+5:30
#define NTP_SERVER "pool.ntp.org"

// ── Camera ──────────────────────────────────────────────────────────────────
// The screen also shows the camera's road width, from the same broadcasts
// the main unit hears
#define CAMERA_UDP_PORT 4210
#define CAMERA_FRESH_MS 3000
#define NARROW_MAX_WIDTH_M 4.0f

// ── Map ─────────────────────────────────────────────────────────────────────
// Pixels per metre: closer in a narrow road or at an alert, where lanes are
// only a few metres wide, like the website's map zooming in
#define MAP_SCALE_CLOSE 6.0f
#define MAP_SCALE_GHAT 2.2f  // the next hairpin can be 250 m ahead
#define MAP_SCALE_NORMAL 1.6f

// Danger alerts flash their border this often, since there's no sound
#define ALERT_FLASH_MS 450
