#pragma once

// Health unit: fuel level (ultrasonic, on a demo tank) and battery readings
// (INA219), broadcast on the hotspot for the car screen.
//
// It doesn't talk to the server: the website stays exactly as it is.

#include <Arduino.h>

// ── Pins (ordinary ESP32 DevKit, all on its right-hand side) ────────────────
#define US_TRIG_PIN 5    // HC-SR04 Trig  -> D5
#define US_ECHO_PIN 18   // HC-SR04 Echo  -> D18 (power the sensor from 3V3)
#define INA_SDA_PIN 21   // INA219 SDA    -> D21
#define INA_SCL_PIN 22   // INA219 SCL    -> D22

// ── Sensors ─────────────────────────────────────────────────────────────────
#define INA219_ADDRESS 0x40   // the board's default (A0 and A1 not bridged)
#define INA219_SHUNT_OHMS 0.1f // the R100 resistor on the board
#define AIR_TEMP_C 30.0f      // sound is a little faster in warm air

// ── Battery defaults (change any time with the cells and mah commands) ──────
#define BATTERY_CELLS 4       // cells in series: 4 for 14.8 V, 2 for 7.4 V
#define BATTERY_MAH 3000      // capacity of one series string, in mAh

// ── Alerts shown on the screen ──────────────────────────────────────────────
#define FUEL_LOW_PERCENT 15
#define BATTERY_LOW_PERCENT 20

// ── Sending ─────────────────────────────────────────────────────────────────
#define UDP_PORT 4210          // the same port the camera uses
#define SEND_INTERVAL_MS 1000
