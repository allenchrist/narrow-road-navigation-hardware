#include "display_hw.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "board.h"

namespace {

Arduino_ESP32RGBPanel *panel = nullptr;
Arduino_RGB_Display *gfx = nullptr;
TAMC_GT911 touch(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, LCD_WIDTH, LCD_HEIGHT);

lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t displayDriver;
lv_indev_drv_t touchDriver;

// LVGL draws into this strip, then it's copied to the panel's frame buffer
constexpr int kBufferLines = 40;

void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(&pixels->full), w, h);
  lv_disp_flush_ready(driver);
}

void readTouch(lv_indev_drv_t *, lv_indev_data_t *data) {
  touch.read();
  if (touch.isTouched && touch.touches > 0) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touch.points[0].x;
    data->point.y = touch.points[0].y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

}  // namespace

bool displayBegin() {
  panel = new Arduino_ESP32RGBPanel(
      LCD_DE, LCD_VSYNC, LCD_HSYNC, LCD_PCLK,
      LCD_R0, LCD_R1, LCD_R2, LCD_R3, LCD_R4,
      LCD_G0, LCD_G1, LCD_G2, LCD_G3, LCD_G4, LCD_G5,
      LCD_B0, LCD_B1, LCD_B2, LCD_B3, LCD_B4,
      LCD_HSYNC_POLARITY, LCD_HSYNC_FRONT_PORCH, LCD_HSYNC_PULSE_WIDTH, LCD_HSYNC_BACK_PORCH,
      LCD_VSYNC_POLARITY, LCD_VSYNC_FRONT_PORCH, LCD_VSYNC_PULSE_WIDTH, LCD_VSYNC_BACK_PORCH,
      LCD_PCLK_ACTIVE_NEG, LCD_PCLK_HZ);
  gfx = new Arduino_RGB_Display(LCD_WIDTH, LCD_HEIGHT, panel, 0, true);

  if (!gfx->begin()) {
    Serial.println("[Display] Panel didn't start; check board.h");
    return false;
  }
  gfx->fillScreen(0x0000);

  pinMode(LCD_BACKLIGHT, OUTPUT);
  digitalWrite(LCD_BACKLIGHT, HIGH);

  touch.begin();
  touch.setRotation(TOUCH_ROTATION);

  lv_init();

  size_t pixels = LCD_WIDTH * kBufferLines;
  auto *buffer = static_cast<lv_color_t *>(
      heap_caps_malloc(pixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!buffer) {
    buffer = static_cast<lv_color_t *>(ps_malloc(pixels * sizeof(lv_color_t)));
  }
  lv_disp_draw_buf_init(&drawBuffer, buffer, nullptr, pixels);

  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = LCD_WIDTH;
  displayDriver.ver_res = LCD_HEIGHT;
  displayDriver.flush_cb = flush;
  displayDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&displayDriver);

  lv_indev_drv_init(&touchDriver);
  touchDriver.type = LV_INDEV_TYPE_POINTER;
  touchDriver.read_cb = readTouch;
  lv_indev_drv_register(&touchDriver);

  Serial.println("[Display] Ready");
  return true;
}

void displayLoop() { lv_timer_handler(); }
