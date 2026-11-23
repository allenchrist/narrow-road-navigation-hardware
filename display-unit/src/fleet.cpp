#include "fleet.h"

namespace {

constexpr float kMovingSpeedMps = 1.0f;
constexpr float kGpsDirectionSpeedMps = 2.0f;

std::string str(JsonVariantConst v) {
  const char *s = v.as<const char *>();
  return s ? std::string(s) : std::string();
}

bool readPoint(JsonVariantConst p, LatLon &out) {
  if (!p.is<JsonArrayConst>() || p.size() < 2) return false;
  out.lat = p[0].as<double>();
  out.lon = p[1].as<double>();
  return true;
}

Path readPath(JsonVariantConst arr) {
  Path path;
  for (JsonVariantConst p : arr.as<JsonArrayConst>()) {
    LatLon ll;
    if (readPoint(p, ll)) path.push_back(ll);
  }
  return path;
}

void readVehicle(JsonObjectConst o, Vehicle &v) {
  v.vehicleId = str(o["vehicleId"]);
  v.vehicleType = o["vehicleType"].isNull() ? "car" : str(o["vehicleType"]);
  v.hasPosition = o["lat"].is<double>() && o["lon"].is<double>();
  v.lat = o["lat"] | 0.0;
  v.lon = o["lon"] | 0.0;
  v.heading = o["heading"] | 0.0f;
  v.hasSpeed = o["speed"].is<float>();
  v.speed = o["speed"] | 0.0f;
  v.hasBearing = o["bearing"].is<float>();
  v.bearing = o["bearing"] | 0.0f;
  v.active = o["active"] | false;
  v.weakSignal = o["weakSignal"] | false;
  v.inSameRoad = o["inSameRoad"] | false;
  v.queuePosition = o["queuePosition"] | 0;
}

void readAlert(JsonObjectConst a, Alert &alert) {
  alert = Alert();
  if (a.isNull()) return;
  alert.kind = a["kind"].isNull() ? "NONE" : str(a["kind"]);
  alert.level = str(a["level"]);
  alert.otherVehicleType = str(a["otherVehicleType"]);
  alert.otherCount = a["otherCount"] | 1;
  alert.reason = str(a["reason"]);
  alert.queuePosition = a["queuePosition"] | 0;
  alert.aheadSameEnd = a["aheadSameEnd"] | 0;
  alert.aheadOtherEnd = a["aheadOtherEnd"] | 0;
  alert.priorityAhead = a["priorityAhead"] | false;
  alert.byPriority = a["byPriority"] | false;
  alert.roadName = str(a["roadName"]);
  alert.bendNumber = a["bendNumber"] | 0;
  alert.hasWaitPoint = readPoint(a["waitPoint"], alert.waitPoint);
}

void readMountain(JsonObjectConst m, MountainInfo &info) {
  info = MountainInfo();
  if (m.isNull()) return;
  info.active = true;
  info.roadName = str(m["roadName"]);
  info.bendCount = m["bendCount"] | 0;
  info.heading = str(m["heading"]);
  info.inBend = m["inBend"] | 0;
  JsonObjectConst next = m["nextBend"];
  if (!next.isNull()) {
    info.hasNextBend = true;
    info.nextBendNumber = next["number"] | 0;
    info.nextBendDistanceM = next["distanceM"] | 0;
    info.nextBendZone = readPath(next["zone"]);
  }
}

}  // namespace

bool parseFleet(JsonVariantConst vehicles, const std::string &myVehicleId, Fleet &out) {
  out = Fleet();
  JsonArrayConst list = vehicles.as<JsonArrayConst>();
  if (list.isNull()) return false;

  bool first = true;
  for (JsonObjectConst o : list) {
    std::string id = str(o["vehicleId"]);
    bool own = !out.hasOwn && (myVehicleId.empty() ? first : id == myVehicleId);
    first = false;

    if (own) {
      out.hasOwn = true;
      OwnVehicle &v = out.own;
      readVehicle(o, v);
      readAlert(o["narrowRoadAlert"], v.alert);
      readMountain(o["mountain"], v.mountain);
      for (JsonVariantConst road : o["currentRoads"].as<JsonArrayConst>()) {
        v.currentRoads.push_back(readPath(road["polygon"]));
      }
      JsonObjectConst entrance = o["entranceRoad"];
      if (!entrance.isNull()) {
        v.hasEntranceRoad = true;
        v.entranceRoad = readPath(entrance["polygon"]);
        v.entranceStatus = str(entrance["status"]);
        readPoint(entrance["entrance"], v.entrancePoint);
      }
    } else {
      Vehicle v;
      readVehicle(o, v);
      out.others.push_back(v);
    }
  }
  return true;
}

bool isMoving(const Vehicle &v) { return v.hasSpeed && v.speed >= kMovingSpeedMps; }

float travelDirection(const Vehicle &v) {
  if (v.hasSpeed && v.speed >= kGpsDirectionSpeedMps && v.hasBearing) return v.bearing;
  return v.heading;
}
