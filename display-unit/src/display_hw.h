#pragma once

// Starts the 5-inch panel, its touch controller and LVGL.
// Pins come from include/board.h.
bool displayBegin();

// Call often: lets LVGL redraw and read touches
void displayLoop();
