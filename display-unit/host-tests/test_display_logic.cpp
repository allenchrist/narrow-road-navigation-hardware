// Checks the screen's reading of server messages and its alert wording, on a
// laptop. The fixtures are real vehicles:update messages recorded from the
// server while its own simulators drove through every alert, and the text the
// website's NarrowRoadAlert component shows for each of them.
//
// From this folder (ArduinoJson is header-only; point -I at its src folder):
//   g++ -std=c++17 -I../src -I<ArduinoJson>/src test_display_logic.cpp
//       ../src/fleet.cpp ../src/alert_text.cpp -o t && ./t fixtures

#include <ArduinoJson.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "../src/alert_text.h"
#include "../src/fleet.h"

static std::string slurp(const std::string &path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static int failures = 0;

static void expect(bool ok, const std::string &what) {
  if (!ok) {
    std::printf("    FAIL %s\n", what.c_str());
    failures++;
  }
}

int main(int argc, char **argv) {
  std::string folder = argc > 1 ? argv[1] : "fixtures";
  JsonDocument updates, website;
  if (deserializeJson(updates, slurp(folder + "/server_updates.json")) ||
      deserializeJson(website, slurp(folder + "/website_text.json"))) {
    std::printf("Can't read the fixtures in %s\n", folder.c_str());
    return 1;
  }

  int cases = 0;
  for (JsonPairConst sample : updates.as<JsonObjectConst>()) {
    std::string key = sample.key().c_str();
    std::string myId = sample.value()["vehicleId"] | "";
    cases++;

    Fleet fleet;
    bool parsed = parseFleet(sample.value()["vehicles"], myId, fleet);
    std::printf("%s\n", key.c_str());
    expect(parsed && fleet.hasOwn && fleet.own.vehicleId == myId, "finds the driver's own vehicle");

    // The parsed alert and shapes match the raw message
    JsonObjectConst raw;
    for (JsonObjectConst v : sample.value()["vehicles"].as<JsonArrayConst>()) {
      if (myId == (v["vehicleId"] | "")) raw = v;
    }
    std::string rawKind = raw["narrowRoadAlert"]["kind"] | "NONE";
    expect(fleet.own.alert.kind == rawKind, "alert kind " + rawKind);
    expect(fleet.own.currentRoads.size() == raw["currentRoads"].size(), "road outlines");
    expect(fleet.own.hasEntranceRoad == !raw["entranceRoad"].isNull(), "entrance road");
    if (fleet.own.hasEntranceRoad) {
      expect(fleet.own.entranceRoad.size() == raw["entranceRoad"]["polygon"].size() &&
                 !fleet.own.entranceStatus.empty(),
             "entrance road shape and status");
    }
    expect(fleet.own.mountain.active == !raw["mountain"].isNull(), "Mountain Mode");
    if (fleet.own.mountain.active) {
      expect(fleet.own.mountain.bendCount > 0 && !fleet.own.mountain.roadName.empty(),
             "ghat road name and hairpins");
    }
    expect(fleet.others.size() + 1 == sample.value()["vehicles"].size(), "other vehicles");

    // The words match the website exactly
    AlertView view = describeAlert(fleet.own.alert, fleet.own);
    JsonObjectConst web = website[key];
    std::string title = web["title"] | "", detail = web["detail"] | "", note = web["note"] | "";
    std::printf("  screen:  %s | %s | %s\n", view.title.c_str(), view.detail.c_str(), view.note.c_str());
    expect(view.show == !title.empty(), "card shown when the website shows one");
    expect(view.title == title, "title matches the website: " + title);
    expect(view.detail == detail, "detail matches the website: " + detail);
    expect(view.note == note, "note matches the website: " + note);
  }

  std::printf(failures ? "\n%d check(s) failed\n" : "\nAll %d messages read and worded like the website\n",
              failures ? failures : cases);
  return failures ? 1 : 0;
}
