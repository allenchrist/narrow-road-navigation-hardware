#pragma once

#include <string>

#include "fleet.h"

// The car screen, laid out like the website's dashboard on 800x480:
//   top bar    name, server and GPS status, clock, light/dark button
//   map        your car in the middle, narrow road outlines, the road ahead
//              at an entrance, other cars with queue numbers, hairpins
//   alert card over the top of the map: icon, title, detail, note, the
//              give-way road picture and steps
//   side panel heading, road, vehicles in this road, Mountain Mode, camera
void uiBegin();

void uiShowFleet(const Fleet &fleet);

// online: connected to the server. problem: why there's no car to show yet,
// such as "Waiting for the main unit to connect", or empty.
void uiShowStatus(bool online, const std::string &problem);

// The camera's latest road width; fresh is false when it's gone quiet
void uiShowCamera(bool fresh, float widthM, bool wide);

// Call often: clock, flashing danger alerts
void uiTick();
