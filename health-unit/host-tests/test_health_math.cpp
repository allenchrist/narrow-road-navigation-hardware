// Tests for the fuel and battery arithmetic, on a laptop:
//   g++ -std=c++17 -Wall -Ihealth-unit/src health-unit/host-tests/test_health_math.cpp health-unit/src/health_math.cpp -o health && ./health

#include <math.h>
#include <stdio.h>

#include "health_math.h"

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "pass" : "FAIL", what);
  if (!ok) failures++;
}

bool near(float a, float b, float tolerance) { return fabsf(a - b) <= tolerance; }

}  // namespace

int main() {
  // Echo timing: 1 m away is about 5.83 ms there and back at 20 C
  check(near(echoToCm(5831, 20.0f), 100.0f, 0.5f), "echo: 5831 us at 20 C is 100 cm");
  check(near(echoToCm(0), 0, 0.001f), "echo: 0 us is 0 cm");

  // Median ignores one wild reading
  float readings[] = {20.1f, 20.3f, 95.0f, 20.2f, 3.0f};
  check(near(medianOf(readings, 5), 20.2f, 0.001f), "median: ignores the two wild echoes");
  float pair[] = {10, 20};
  check(near(medianOf(pair, 2), 15, 0.001f), "median: even count averages the middle two");

  // Tank: sensor 30 cm above the bottom, 5 cm above the surface when full
  TankCalibration tank;
  check(fuelPercent(tank, 10) < 0, "tank: not calibrated gives -1");
  tank.emptyCm = 30;
  tank.fullCm = 5;
  check(near(fuelPercent(tank, 30), 0, 0.01f), "tank: at the bottom is 0%");
  check(near(fuelPercent(tank, 5), 100, 0.01f), "tank: at the full mark is 100%");
  check(near(fuelPercent(tank, 17.5f), 50, 0.01f), "tank: halfway is 50%");
  check(near(fuelPercent(tank, 40), 0, 0.01f), "tank: further than empty stays 0%");
  check(near(fuelPercent(tank, 2), 100, 0.01f), "tank: closer than full stays 100%");
  check(fuelLitres(tank, 50) < 0, "tank: litres unknown without a capacity");
  tank.litres = 4;
  check(near(fuelLitres(tank, 50), 2, 0.001f), "tank: half of 4 litres is 2");
  TankCalibration backwards;
  backwards.emptyCm = 5;
  backwards.fullCm = 30;
  check(!backwards.valid(), "tank: full further away than empty is rejected");

  // One cell's charge from its voltage
  check(near(cellPercentFromVoltage(4.2f), 100, 0.01f), "cell: 4.20 V is full");
  check(near(cellPercentFromVoltage(3.0f), 0, 0.01f), "cell: 3.00 V is empty");
  check(near(cellPercentFromVoltage(3.79f), 50, 0.01f), "cell: 3.79 V is about half");
  check(near(cellPercentFromVoltage(4.5f), 100, 0.01f), "cell: above 4.2 V stays 100");
  check(near(cellPercentFromVoltage(2.5f), 0, 0.01f), "cell: below 3.0 V stays 0");

  // Gauge, 4 cells in series, 3000 mAh
  BatteryGauge g;
  g.configure(4, 3000);
  g.update(4 * 3.79f, 0, 0);
  check(near(g.percent(), 50, 0.5f), "gauge: starts from the voltage (15.16 V is about 50%)");

  // 1 hour at 450 mA (heavy enough not to drift to the voltage) uses 15%
  BatteryGauge h;
  h.configure(4, 3000);
  h.update(4 * 3.79f, 450, 0);
  for (int i = 0; i < 3600; i++) h.update(4 * 3.79f, 450, 1000);
  check(near(h.percent(), 35, 0.5f), "gauge: 450 mA for an hour takes 15% of 3000 mAh");
  check(near(h.minutesLeft(450), 140, 3), "gauge: 35% of 3000 mAh at 450 mA lasts 140 min");
  check(h.minutesLeft(5) < 0, "gauge: no time left shown when not discharging");

  // Light load: drifts back to the voltage estimate
  BatteryGauge d;
  d.configure(4, 3000);
  d.update(4 * 3.79f, 0, 0);
  for (int i = 0; i < 1200; i++) d.update(4 * 4.0f, 50, 1000);
  check(near(d.percent(), 80, 3), "gauge: light load settles at the voltage's 80%");

  // Two cells in series reads the same per cell
  BatteryGauge two;
  two.configure(2, 6000);
  two.update(2 * 4.0f, 0, 0);
  check(near(two.percent(), 80, 0.5f), "gauge: 2 cells at 8.0 V is 80%");

  if (failures) {
    printf("\n%d health test(s) FAILED\n", failures);
    return 1;
  }
  printf("\nAll health tests passed\n");
  return 0;
}
