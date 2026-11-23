#pragma once

// The words, colour and picture for each alert, the same as the website's
// NarrowRoadAlert.jsx. Plain C++, so the host tests can check it.

#include <cstdint>
#include <string>

#include "fleet.h"

enum class Tone { Stop, Go, Clear, Warning, Caution };

enum class AlertIcon { Yield, ArrowUp, Car, Check, Road, Warning };

// The narrow road seen from the side: the other vehicle on the left, yours
// on the right, and which of you goes
struct AlertScene {
  bool show = false;
  std::string otherLabel;  // "Entered first"
  std::string ownLabel;    // "Wait here"
  int goes = 0;            // -1: the other vehicle, 1: you, 0: neither
  bool stopLine = false;
  bool outside = false;    // the other vehicle is outside the road
};

struct AlertView {
  bool show = false;
  Tone tone = Tone::Caution;
  AlertIcon icon = AlertIcon::Road;
  std::string title;
  std::string detail;
  std::string note;
  AlertScene scene;
  int step = -1;  // give-way step: 0 Stop, 1 Let it pass, 2 Continue
};

AlertView describeAlert(const Alert &alert, const Vehicle &own);

// "the bus", or "other vehicles"
std::string otherName(const Alert &alert);

std::string ordinal(int n);

// "Car", "Bus", "Priority vehicle"...
std::string vehicleLabel(const std::string &type);

// Imported roads without a real name are stored as "Narrow Road 12345"
bool hasRoadName(const std::string &name);

// The panel's side list: true for alerts shown while inside a narrow road
bool isInsideRoadAlert(const std::string &kind);

// Colour for a tone, as 0xRRGGBB, matching the website
uint32_t toneColour(Tone tone);

// Colour for a narrow road's status at an entrance
uint32_t roadStatusColour(const std::string &status);
