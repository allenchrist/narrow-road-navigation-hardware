#pragma once

#include <string>

#include "fleet.h"

// The car screen, laid out like the website's dashboard on 800x480:
//   top bar    name, server and GPS status, clock, light/dark button
//   map        your car in the middle, narrow road outlines, the road ahead
//              at an entrance, other cars with queue numbers, hairpins
//   alert card over the top of the map: icon, title, detail, note, the
//              give-way road picture and steps
//   side panel heading, road, vehicles in this road, Mountain Mode, camera,
//              fuel and battery
void uiBegin();

void uiShowFleet(const Fleet &fleet);

// online: connected to the server. problem: why there's no car to show yet,
// such as "Waiting for the main unit to connect", or empty.
void uiShowStatus(bool online, const std::string &problem);

// The camera's latest road width; fresh is false when it's gone quiet
void uiShowCamera(bool fresh, float widthM, bool wide);

// The heading straight from the main unit on the hotspot (faster than the
// server, and works without internet). fresh is false when it's gone quiet,
// and the panel goes back to the server's direction.
void uiShowLocalHeading(bool fresh, float heading);

// From the health unit. fresh is false when it's gone quiet.
// percent and litres are -1 when unknown.
void uiShowFuel(bool fresh, bool ok, bool calibrated, float percent, float litres, bool low);
void uiShowBattery(bool fresh, bool ok, float volts, float amps, float percent,
                   float minutesLeft, bool low);

// Call often: clock, flashing danger alerts
void uiTick();
