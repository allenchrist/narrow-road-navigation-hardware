// Draws the car screen on a laptop, for each recorded server message,
// into PPM pictures. Run through preview.sh.
#include <ArduinoJson.h>
#include <lvgl.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include "ui.h"
static auto t0 = std::chrono::steady_clock::now();
static uint32_t fakeOffset = 0;
extern "C" uint32_t millis(void) { return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() + fakeOffset; }
bool getLocalTime(struct tm *t, uint32_t) { memset(t, 0, sizeof(*t)); t->tm_hour = 9; t->tm_min = 41; return true; }
// The panel, as RGB565 pixels
static uint16_t fb[800 * 480];
static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *p) {
  for (int y = a->y1; y <= a->y2; y++) for (int x = a->x1; x <= a->x2; x++) fb[y * 800 + x] = (p++)->full;
  lv_disp_flush_ready(d);
}
static void save(const std::string &path) {
  std::ofstream f(path, std::ios::binary); f << "P6\n800 480\n255\n";
  for (int i = 0; i < 800 * 480; i++) { uint16_t c = fb[i]; unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)}; f.write((char*)rgb, 3); }
}
static void pump(int ms) { for (int i = 0; i < ms / 10; i++) { fakeOffset += 10; uiTick(); lv_timer_handler(); } }
int main(int argc, char **argv) {
  lv_init();
  static lv_color_t buf[800 * 40]; static lv_disp_draw_buf_t db; lv_disp_draw_buf_init(&db, buf, nullptr, 800 * 40);
  static lv_disp_drv_t dd; lv_disp_drv_init(&dd); dd.hor_res = 800; dd.ver_res = 480; dd.flush_cb = flush; dd.draw_buf = &db; lv_disp_drv_register(&dd);
  uiBegin();
  std::ifstream in(argv[1]); std::stringstream ss; ss << in.rdbuf();
  JsonDocument doc; deserializeJson(doc, ss.str());
  uiShowStatus(false, "Waiting for the main unit to connect"); uiShowCamera(false, 0, false); pump(300); save(std::string(argv[2]) + "/waiting.ppm");
  for (JsonPairConst s : doc.as<JsonObjectConst>()) {
    Fleet f; parseFleet(s.value()["vehicles"], s.value()["vehicleId"] | "", f);
    uiShowStatus(true, ""); uiShowCamera(true, 3.1f, false); uiShowFleet(f);
    uiShowFuel(true, true, true, 62, 3.1f, false); uiShowBattery(true, true, 15.2f, 0.42f, 71, 380, false); pump(200);
    std::string k = s.key().c_str(); for (char &c : k) if (c == '+') c = '_';
    save(std::string(argv[2]) + "/" + k + ".ppm");
  }
  return 0;
}
