#pragma once

// What the server sends a car screen in "vehicles:update", as plain structs.
// Plain C++ (no Arduino code), so the host tests can use it.

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

struct LatLon {
  double lat = 0;
  double lon = 0;
};

using Path = std::vector<LatLon>;

// The driver's alert, the same fields the website reads
struct Alert {
  std::string kind = "NONE";  // GIVE_WAY, ENTRY_WAIT, BEND_STOP...
  std::string level;
  std::string otherVehicleType;
  int otherCount = 1;
  std::string reason;  // ENTRY_WAIT: OCCUPIED, ENTERING or TURN
  int queuePosition = 0;
  int aheadSameEnd = 0;
  int aheadOtherEnd = 0;
  bool priorityAhead = false;
  bool byPriority = false;
  std::string roadName;
  int bendNumber = 0;
  bool hasWaitPoint = false;
  LatLon waitPoint;
};

struct MountainInfo {
  bool active = false;
  std::string roadName;
  int bendCount = 0;
  std::string heading;  // UP, DOWN or empty
  int inBend = 0;
  bool hasNextBend = false;
  int nextBendNumber = 0;
  int nextBendDistanceM = 0;
  Path nextBendZone;
};

struct Vehicle {
  std::string vehicleId;
  std::string vehicleType;
  bool hasPosition = false;
  double lat = 0, lon = 0;
  float heading = 0;
  bool hasSpeed = false;
  float speed = 0;
  bool hasBearing = false;
  float bearing = 0;
  bool active = false;
  bool weakSignal = false;
  // Other vehicles only
  bool inSameRoad = false;
  int queuePosition = 0;
};

struct OwnVehicle : Vehicle {
  Alert alert;
  MountainInfo mountain;
  std::vector<Path> currentRoads;  // narrow roads it's inside
  bool hasEntranceRoad = false;    // at an entrance: the road ahead
  Path entranceRoad;
  std::string entranceStatus;  // AVAILABLE, RESERVED or OCCUPIED
  LatLon entrancePoint;
};

struct Fleet {
  bool hasOwn = false;
  OwnVehicle own;
  std::vector<Vehicle> others;
};

// Reads the "vehicles" array of a vehicles:update event. The driver's own
// vehicle is the one with myVehicleId, or the first if that's empty.
bool parseFleet(JsonVariantConst vehicles, const std::string &myVehicleId, Fleet &out);

// The direction the car is going: GPS when moving, compass otherwise,
// the same rule as the website's travelDirection()
float travelDirection(const Vehicle &v);

bool isMoving(const Vehicle &v);
