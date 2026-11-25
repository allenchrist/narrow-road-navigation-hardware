// Narrow Road Navigation: health unit
//
// A separate ESP32 that measures:
//   - the fuel level of a demo tank, with an HC-SR04 ultrasonic sensor
//     looking down from the top of the tank
//   - the battery's voltage, current and power, with an INA219, and works
//     out its charge and time left
// and broadcasts both on the hotspot once a second (UDP port 4210), where
// the car screen hears them, the same way the camera sends road widths.
//
// It never talks to the server: the website stays exactly as it is.
//
// Settings: include/config.h and include/secrets.h
// Type help in the serial monitor for the tank and battery commands.

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>

#include "config.h"
#include "health_math.h"
#include "ina219.h"
#include "secrets.h"
#include "ultrasonic.h"

namespace {

Ultrasonic sonar;
Ina219 ina;
BatteryGauge gauge;
TankCalibration tank;
WiFiUDP udp;
Preferences prefs;

float batteryVolts = 0;
float batteryMa = 0;
uint32_t lastBatteryMs = 0;
uint32_t lastInaRetryMs = 0;
uint32_t lastSendMs = 0;
uint32_t lastStatusMs = 0;
uint32_t lastWatchMs = 0;
bool watching = false;
bool wifiWasUp = false;
String commandLine;

// ── Saved settings ──────────────────────────────────────────────────────────

void loadSettings() {
  prefs.begin("health", false);
  tank.emptyCm = prefs.getFloat("empty", 0);
  tank.fullCm = prefs.getFloat("full", 0);
  tank.litres = prefs.getFloat("litres", 0);
  int cells = prefs.getInt("cells", BATTERY_CELLS);
  float mah = prefs.getFloat("mah", BATTERY_MAH);
  gauge.configure(cells, mah);
}

void printSettings() {
  Serial.printf("[Tank] empty %.1f cm, full %.1f cm, capacity %s", tank.emptyCm, tank.fullCm,
                tank.litres > 0 ? "" : "not set");
  if (tank.litres > 0) Serial.printf("%.2f litres", tank.litres);
  Serial.println(tank.valid() ? "" : " | not calibrated: type empty, then full");
  Serial.printf("[Battery] %d cells in series, %.0f mAh\n", gauge.cells(), gauge.capacityMah());
}

// ── Readings ────────────────────────────────────────────────────────────────

void updateBattery() {
  uint32_t now = millis();

  if (!ina.ok()) {
    if (now - lastInaRetryMs > 5000) {
      lastInaRetryMs = now;
      if (ina.begin(Wire, INA219_ADDRESS, INA219_SHUNT_OHMS)) {
        Serial.println("[Battery] INA219 found");
      }
    }
    return;
  }

  if (now - lastBatteryMs < 250) return;
  uint32_t dt = lastBatteryMs ? now - lastBatteryMs : 0;
  lastBatteryMs = now;

  float v, ma;
  if (!ina.read(v, ma)) {
    Serial.println("[Battery] INA219 stopped answering; check SDA, SCL, VCC and GND");
    return;
  }
  batteryVolts = v;
  batteryMa = ma;

  // Below a volt there's no battery connected: don't invent a charge
  if (v > 1.0f) gauge.update(v, ma, dt);
}

bool batteryPresent() { return ina.ok() && batteryVolts > 1.0f && gauge.started(); }

float currentFuelPercent() { return sonar.ok() ? fuelPercent(tank, sonar.distanceCm()) : -1; }

// ── Sending to the screen ───────────────────────────────────────────────────

void sendPacket(const char *json) {
  udp.beginPacket(WiFi.broadcastIP(), UDP_PORT);
  udp.write(reinterpret_cast<const uint8_t *>(json), strlen(json));
  udp.endPacket();
}

void sendReadings() {
  char msg[256];

  float percent = currentFuelPercent();
  float litres = fuelLitres(tank, percent);
  snprintf(msg, sizeof(msg),
           "{\"type\":\"fuel\",\"ok\":%s,\"calibrated\":%s,\"percent\":%.1f,\"litres\":%.2f,"
           "\"cm\":%.1f,\"low\":%s}",
           sonar.ok() ? "true" : "false", tank.valid() ? "true" : "false", percent, litres,
           sonar.ok() ? sonar.distanceCm() : -1.0f,
           percent >= 0 && percent < FUEL_LOW_PERCENT ? "true" : "false");
  sendPacket(msg);

  bool present = batteryPresent();
  float minutes = present ? gauge.minutesLeft(batteryMa) : -1;
  snprintf(msg, sizeof(msg),
           "{\"type\":\"battery\",\"ok\":%s,\"volts\":%.2f,\"amps\":%.3f,\"watts\":%.2f,"
           "\"percent\":%.0f,\"minutesLeft\":%.0f,\"cells\":%d,\"low\":%s}",
           present ? "true" : "false", batteryVolts, batteryMa / 1000.0f,
           batteryVolts * batteryMa / 1000.0f, present ? gauge.percent() : -1.0f, minutes,
           gauge.cells(), present && gauge.percent() < BATTERY_LOW_PERCENT ? "true" : "false");
  sendPacket(msg);
}

// ── Serial monitor ──────────────────────────────────────────────────────────

void printReadings(const char *tag) {
  Serial.printf("[%s] fuel: ", tag);
  if (!sonar.ok()) {
    Serial.print("no echo (check the sensor's wires and aim)");
  } else {
    float p = currentFuelPercent();
    Serial.printf("%.1f cm", sonar.distanceCm());
    if (p >= 0) Serial.printf(", %.0f%%", p);
    float l = fuelLitres(tank, p);
    if (l >= 0) Serial.printf(", %.2f L", l);
  }
  Serial.print(" | battery: ");
  if (!ina.ok()) {
    Serial.print("INA219 missing");
  } else if (!batteryPresent()) {
    Serial.printf("no battery (%.2f V)", batteryVolts);
  } else {
    Serial.printf("%.2f V (%.2f V/cell), %.0f mA, %.2f W, %.0f%%", batteryVolts,
                  gauge.cellVolts(batteryVolts), batteryMa, batteryVolts * batteryMa / 1000.0f,
                  gauge.percent());
    float m = gauge.minutesLeft(batteryMa);
    if (m >= 0) Serial.printf(", %.0f min left", m);
  }
  if (strcmp(tag, "Status") == 0) {
    Serial.printf(" | Wi-Fi: %s", WiFi.status() == WL_CONNECTED ? "connected" : "not connected");
  }
  Serial.println();
}

void printHelp() {
  Serial.println("Commands (type one, then press Enter):");
  Serial.println("  empty      the tank is empty now: remember this distance");
  Serial.println("  full       the tank is full now: remember this distance");
  Serial.println("  litres 5   the tank holds 5 litres when full (to show litres)");
  Serial.println("  cells 4    the battery has 4 cells in series (2 for 7.4 V, 1 for 3.7 V)");
  Serial.println("  mah 3000   the battery's capacity in mAh");
  Serial.println("  watch      show the readings 5 times a second; press Enter to stop");
  Serial.println("  reset      forget the tank and battery settings");
  Serial.println("  help       this list");
}

void runCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (watching) {
    watching = false;
    Serial.println("[Watch] Stopped");
    if (cmd.isEmpty() || cmd == "watch") return;
  }
  if (cmd.isEmpty()) return;

  int space = cmd.indexOf(' ');
  String word = space < 0 ? cmd : cmd.substring(0, space);
  String arg = space < 0 ? "" : cmd.substring(space + 1);
  arg.trim();

  if (word == "empty" || word == "full") {
    if (!sonar.ok()) {
      Serial.println("[Tank] No echo from the sensor; check its wiring first");
      return;
    }
    float d = sonar.distanceCm();
    if (word == "empty") {
      tank.emptyCm = d;
      prefs.putFloat("empty", d);
    } else {
      tank.fullCm = d;
      prefs.putFloat("full", d);
    }
    Serial.printf("[Tank] Saved: %s is %.1f cm from the sensor\n", word.c_str(), d);
    if (tank.emptyCm > 0 && tank.fullCm > 0 && !tank.valid()) {
      Serial.println("[Tank] Full must be at least 2 cm closer than empty; do both again");
    }
    printSettings();
  } else if (word == "litres") {
    float l = arg.toFloat();
    if (l <= 0) {
      Serial.println("[Tank] Type the capacity, such as: litres 5");
      return;
    }
    tank.litres = l;
    prefs.putFloat("litres", l);
    printSettings();
  } else if (word == "cells") {
    int n = arg.toInt();
    if (n < 1 || n > 6) {
      Serial.println("[Battery] Type the number of cells in series, 1 to 6, such as: cells 4");
      return;
    }
    gauge.configure(n, gauge.capacityMah());
    prefs.putInt("cells", n);
    printSettings();
  } else if (word == "mah") {
    float mah = arg.toFloat();
    if (mah < 100) {
      Serial.println("[Battery] Type the capacity in mAh, such as: mah 3000");
      return;
    }
    gauge.configure(gauge.cells(), mah);
    prefs.putFloat("mah", mah);
    printSettings();
  } else if (word == "watch") {
    watching = true;
    Serial.println("[Watch] Press Enter to stop");
  } else if (word == "reset") {
    prefs.clear();
    tank = TankCalibration();
    gauge.configure(BATTERY_CELLS, BATTERY_MAH);
    Serial.println("[Settings] Cleared");
    printSettings();
  } else if (word == "help") {
    printHelp();
  } else {
    Serial.printf("Unknown command: %s\n", cmd.c_str());
    printHelp();
  }
}

void readSerialCommands() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      runCommand(commandLine);
      commandLine = "";
    } else if (c == '\b' || c == 127) {  // Backspace
      if (commandLine.length() > 0) commandLine.remove(commandLine.length() - 1);
    } else if (commandLine.length() < 32) {
      commandLine += c;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nNarrow Road Navigation: health unit");

  loadSettings();
  printSettings();

  sonar.begin(US_TRIG_PIN, US_ECHO_PIN, AIR_TEMP_C);

  Wire.begin(INA_SDA_PIN, INA_SCL_PIN);
  if (ina.begin(Wire, INA219_ADDRESS, INA219_SHUNT_OHMS)) {
    Serial.println("[Battery] INA219 ready");
  } else {
    Serial.println("[Battery] INA219 not found at 0x40: check SDA, SCL, VCC and GND");
  }
  lastInaRetryMs = millis();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Type help for the tank and battery commands");
}

void loop() {
  uint32_t now = millis();

  readSerialCommands();
  sonar.update();
  updateBattery();

  bool wifiUp = WiFi.status() == WL_CONNECTED;
  if (wifiUp != wifiWasUp) {
    wifiWasUp = wifiUp;
    if (wifiUp) {
      Serial.printf("[Wi-Fi] Connected, address %s; sending to the screen on port %d\n",
                    WiFi.localIP().toString().c_str(), UDP_PORT);
    } else {
      Serial.println("[Wi-Fi] Not connected; trying again");
    }
  }

  if (wifiUp && now - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = now;
    sendReadings();
  }

  if (watching) {
    if (now - lastWatchMs >= 200) {
      lastWatchMs = now;
      printReadings("Watch");
    }
  } else if (now - lastStatusMs >= 5000) {
    lastStatusMs = now;
    printReadings("Status");
  }

  delay(2);
}
