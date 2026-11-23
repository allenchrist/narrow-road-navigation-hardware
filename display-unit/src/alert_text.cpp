#include "alert_text.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace {

std::string capitalize(std::string s) {
  if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
  return s;
}

std::string lower(std::string s) {
  for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string cars(int n) { return n == 1 ? "1 car" : std::to_string(n) + " cars"; }

std::string hairpin(const Alert &a) { return "Hairpin " + std::to_string(a.bendNumber); }

// "You're 3rd in line · 2 cars ahead of you, at the other end"
std::string queueLine(const Alert &a) {
  if (!a.queuePosition) return "You'll be told when it's clear";
  int same = a.aheadSameEnd, other = a.aheadOtherEnd;
  std::string where;
  if (same && other) {
    where = cars(same) + " ahead at your end, " + std::to_string(other) + " at the other end";
  } else if (other) {
    where = cars(other) + " ahead of you, at the other end";
  } else if (same) {
    where = cars(same) + " ahead of you, at your end";
  }
  std::string line;
  if (a.priorityAhead) line = "An emergency vehicle is ahead of you · ";
  line += "You're " + ordinal(a.queuePosition) + " in line";
  if (!where.empty()) line += " · " + where;
  return line;
}

}  // namespace

std::string ordinal(int n) {
  int tens = n % 100;
  const char *suffix = "th";
  if (tens < 11 || tens > 13) {
    switch (n % 10) {
      case 1: suffix = "st"; break;
      case 2: suffix = "nd"; break;
      case 3: suffix = "rd"; break;
    }
  }
  return std::to_string(n) + suffix;
}

std::string vehicleLabel(const std::string &type) {
  if (type == "car") return "Car";
  if (type == "truck") return "Truck";
  if (type == "lorry") return "Lorry";
  if (type == "bus") return "Bus";
  if (type == "auto") return "Auto";
  if (type == "priority") return "Priority vehicle";
  return "";
}

std::string otherName(const Alert &a) {
  if (a.otherCount > 1) return "other vehicles";
  std::string label = vehicleLabel(a.otherVehicleType);
  return label.empty() ? "the other vehicle" : "the " + lower(label);
}

bool hasRoadName(const std::string &name) {
  if (name.empty()) return false;
  const char *prefix = "Narrow Road ";
  size_t n = std::strlen(prefix);
  if (name.compare(0, n, prefix) != 0 || name.size() == n) return true;
  for (size_t i = n; i < name.size(); i++) {
    if (!std::isdigit(static_cast<unsigned char>(name[i]))) return true;
  }
  return false;  // a placeholder name
}

bool isInsideRoadAlert(const std::string &k) {
  return k == "IN_NARROW_ROAD" || k == "VEHICLE_WAITING" || k == "VEHICLE_ENTERING" ||
         k == "RIGHT_OF_WAY" || k == "GIVE_WAY" || k == "ROAD_CLEAR";
}

uint32_t toneColour(Tone tone) {
  switch (tone) {
    case Tone::Stop: return 0xff5c63;
    case Tone::Go:
    case Tone::Clear: return 0x43d17a;
    case Tone::Warning: return 0xff9b3d;
    case Tone::Caution: return 0xf2b84b;
  }
  return 0xf2b84b;
}

uint32_t roadStatusColour(const std::string &status) {
  if (status == "AVAILABLE") return 0x43d17a;
  if (status == "RESERVED") return 0xf2b84b;
  return 0xff5c63;
}

AlertView describeAlert(const Alert &a, const Vehicle &own) {
  AlertView v;
  const std::string &k = a.kind;
  std::string other = otherName(a);
  std::string reason;
  bool priority = a.byPriority;

  // "another vehicle" reads better than "the other vehicle" in some places
  std::string another = other == "the other vehicle" ? "another vehicle" : other;

  v.show = true;

  if (k == "GIVE_WAY") {
    v.icon = AlertIcon::Yield;
    v.tone = Tone::Stop;
    v.title = "Give way";
    v.detail = (priority ? "Pull over and let " : "Stop and let ") + other + " pass";
    reason = other + (priority ? " has right of way" : " entered first");
    v.scene = {true, priority ? "Goes first" : "Entered first", "Wait here", -1, true, false};
    v.step = isMoving(own) ? 0 : 1;
  } else if (k == "RIGHT_OF_WAY") {
    v.icon = AlertIcon::ArrowUp;
    v.tone = Tone::Go;
    v.title = "You have right of way";
    v.detail = priority ? "Continue, others are giving way to you" : "Continue slowly";
    reason = other + " should give way";
    v.scene = {true, "Giving way", priority ? "Goes first" : "Entered first", 1, false, false};
  } else if (k == "VEHICLE_ENTERING") {
    v.icon = AlertIcon::Car;
    v.tone = Tone::Warning;
    v.title = "Vehicle about to enter";
    v.detail = "Slow down, " + another + " is approaching this road";
    v.scene = {true, "Approaching", "In the road", 0, false, true};
  } else if (k == "ROAD_CLEAR") {
    v.icon = AlertIcon::Check;
    v.tone = Tone::Clear;
    v.title = "Road clear";
    v.detail = "You can continue";
    reason = other + " has passed";
    v.step = 2;
  } else if (k == "IN_NARROW_ROAD") {
    v.icon = AlertIcon::Road;
    v.tone = Tone::Caution;
    v.title = "Narrow road";
    v.detail = "Proceed with caution";
  } else if (k == "VEHICLE_WAITING") {
    v.icon = AlertIcon::Car;
    v.tone = Tone::Caution;
    v.title = "Vehicle waiting";
    v.detail = capitalize(another) + " is waiting for you to pass";
  } else if (k == "ENTRY_GO") {
    v.icon = AlertIcon::ArrowUp;
    v.tone = Tone::Go;
    v.title = "Go ahead";
    v.detail = "The narrow road ahead is clear";
  } else if (k == "ENTRY_FOLLOW") {
    v.icon = AlertIcon::ArrowUp;
    v.tone = Tone::Go;
    v.title = "Go ahead";
    v.detail = "Follow " + (other == "the other vehicle" ? std::string("the vehicle") : other) + " ahead";
    reason = a.queuePosition
                 ? "You're " + ordinal(a.queuePosition) + " in line · going through together"
                 : "Going the same way";
  } else if (k == "ENTRY_WAIT") {
    v.icon = AlertIcon::Yield;
    v.tone = Tone::Stop;
    v.title = "Wait at the entrance";
    if (a.reason == "TURN") {
      v.detail = "Cars from the other end have been waiting, so they go next";
    } else if (a.reason == "OCCUPIED") {
      v.detail = capitalize(other) + (a.otherCount > 1 ? " are" : " is") + " in the narrow road";
    } else {
      v.detail = capitalize(other) + " is about to enter from the other end";
    }
    reason = priority ? capitalize(other) + " goes first · " + queueLine(a) : queueLine(a);
  } else if (k == "ENTRY_PRIORITY") {
    v.icon = AlertIcon::Car;
    v.tone = Tone::Warning;
    v.title = "Vehicle in the road";
    v.detail = capitalize(other) + (other == "other vehicles" ? " have" : " has") +
               " been told you're coming";
  } else if (k == "BEND_STOP") {
    v.icon = AlertIcon::Yield;
    v.tone = Tone::Stop;
    v.title = "Stop before the bend";
    v.detail = capitalize(other) + " is coming from the opposite direction";
    reason = "Wait at the marked point · " + hairpin(a);
  } else if (k == "BEND_GO") {
    v.icon = AlertIcon::ArrowUp;
    v.tone = Tone::Go;
    v.title = "Take the bend";
    v.detail = capitalize(other) + " is waiting for you on the other side";
    reason = hairpin(a);
  } else if (k == "BEND_KEEP_LEFT") {
    v.icon = AlertIcon::Car;
    v.tone = Tone::Caution;
    v.title = "Vehicle round the bend";
    v.detail = "A vehicle is coming from the opposite direction. Keep left";
    reason = hairpin(a);
  } else if (k == "BEND_CLEAR") {
    v.icon = AlertIcon::Check;
    v.tone = Tone::Clear;
    v.title = "Bend clear";
    v.detail = "Go ahead";
    reason = hairpin(a);
  } else if (k == "BEND_NO_OVERTAKE") {
    v.icon = AlertIcon::Warning;
    v.tone = Tone::Caution;
    v.title = "Don't overtake";
    v.detail = capitalize(other) + " ahead is turning at the bend";
    reason = hairpin(a);
  } else {
    return AlertView();  // NONE, or a kind this screen doesn't know
  }

  std::string note = reason.empty() ? "" : capitalize(reason);
  if (hasRoadName(a.roadName)) note += (note.empty() ? "" : " · ") + a.roadName;
  v.note = note;
  return v;
}
