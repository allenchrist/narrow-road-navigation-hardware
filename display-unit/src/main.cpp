// Narrow Road Navigation: display unit (the car screen)
//
// Does what the website's dashboard does: logs in with the same account as
// the main unit, receives this car's alert and the vehicles nearby from the
// server, and shows them on the 5-inch screen.
//
// Settings: include/config.h, include/secrets.h, and include/board.h for pins.

#include <Arduino.h>

#include "camera_listener.h"
#include "config.h"
#include "display_hw.h"
#include "server_session.h"
#include "ui.h"

namespace {

ServerSession session;
CameraListener camera;
uint32_t lastStatusMs = 0;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nNarrow Road Navigation: display unit");
  Serial.printf("PSRAM: %s\n", psramFound() ? "found" : "MISSING (check board settings)");

  if (!displayBegin()) {
    Serial.println("Display failed to start");
  }
  uiBegin();
  session.begin();
}

void loop() {
  session.loop();
  camera.update(CAMERA_UDP_PORT);

  Fleet fleet;
  if (session.takeFleet(fleet)) uiShowFleet(fleet);
  uiShowStatus(session.online(), session.problem());

  const WidthReading &w = camera.last();
  bool fresh = camera.everHeard() && millis() - w.receivedMs < CAMERA_FRESH_MS;
  uiShowCamera(fresh, w.width, w.wide);

  // Straight from the other units on the hotspot
  uint32_t now = millis();
  const PositionReading &p = camera.position();
  uiShowLocalHeading(p.receivedMs && now - p.receivedMs < LOCAL_HEADING_FRESH_MS && p.hasHeading,
                     p.heading);
  const FuelReading &f = camera.fuel();
  uiShowFuel(f.receivedMs && now - f.receivedMs < HEALTH_FRESH_MS, f.ok, f.calibrated, f.percent,
             f.litres, f.low);
  const BatteryReading &b = camera.battery();
  uiShowBattery(b.receivedMs && now - b.receivedMs < HEALTH_FRESH_MS, b.ok, b.volts, b.amps,
                b.percent, b.minutesLeft, b.low);

  uiTick();
  displayLoop();

  if (millis() - lastStatusMs > 10000) {
    lastStatusMs = millis();
    Serial.printf("[Status] %s | free heap %u, PSRAM %u\n",
                  session.problem().empty() ? "showing car" : session.problem().c_str(),
                  ESP.getFreeHeap(), ESP.getFreePsram());
  }

  delay(5);
}
