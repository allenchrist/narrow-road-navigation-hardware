#pragma once

// Pins for the 5-inch 800x480 ESP32-S3 board sold as "ESP32-8048S050"
// (Sunton / "Cheap Yellow Display" family, capacitive touch). The 4.3-inch
// ESP32-8048S043 uses the same pins. These match Arduino_GFX's own
// example for that board.
//
// A different board? Copy its pins from the seller's demo code into here.
// Only this file changes.

#define LCD_WIDTH 800
#define LCD_HEIGHT 480

#define LCD_BACKLIGHT 2

#define LCD_DE 40
#define LCD_VSYNC 41
#define LCD_HSYNC 39
#define LCD_PCLK 42
#define LCD_R0 45
#define LCD_R1 48
#define LCD_R2 47
#define LCD_R3 21
#define LCD_R4 14
#define LCD_G0 5
#define LCD_G1 6
#define LCD_G2 7
#define LCD_G3 15
#define LCD_G4 16
#define LCD_G5 4
#define LCD_B0 8
#define LCD_B1 3
#define LCD_B2 46
#define LCD_B3 9
#define LCD_B4 1

#define LCD_HSYNC_POLARITY 0
#define LCD_HSYNC_FRONT_PORCH 8
#define LCD_HSYNC_PULSE_WIDTH 4
#define LCD_HSYNC_BACK_PORCH 8
#define LCD_VSYNC_POLARITY 0
#define LCD_VSYNC_FRONT_PORCH 8
#define LCD_VSYNC_PULSE_WIDTH 4
#define LCD_VSYNC_BACK_PORCH 8
#define LCD_PCLK_ACTIVE_NEG 1
#define LCD_PCLK_HZ 16000000

// GT911 touch controller
#define TOUCH_SDA 19
#define TOUCH_SCL 20
#define TOUCH_INT -1
#define TOUCH_RST 38
// If taps land in the opposite corner of the screen, use ROTATION_INVERTED
#define TOUCH_ROTATION ROTATION_NORMAL
