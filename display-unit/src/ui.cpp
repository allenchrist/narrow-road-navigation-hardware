#include "ui.h"

#include <Arduino.h>
#include <Preferences.h>
#include <lvgl.h>
#include <math.h>
#include <time.h>

#include <algorithm>

#include "alert_text.h"
#include "board.h"
#include "config.h"

namespace {

// ── Layout ──────────────────────────────────────────────────────────────────
constexpr int kTopBarH = 44;
constexpr int kPanelW = 220;
constexpr int kMapW = LCD_WIDTH - kPanelW;
constexpr int kMapH = LCD_HEIGHT - kTopBarH;
constexpr int kCardMargin = 10;
constexpr int kCardW = kMapW - 2 * kCardMargin;

// ── Colours ─────────────────────────────────────────────────────────────────
struct Palette {
  uint32_t background, surface, border, text, muted, mapBackground, mapGrid, road;
};

const Palette kDark = {0x0b0f14, 0x151b23, 0x2a3440, 0xe8edf2, 0x93a1b0, 0x0e141b, 0x18212b, 0x3a4552};
const Palette kLight = {0xf4f6f8, 0xffffff, 0xd5dbe1, 0x1b2430, 0x5b6878, 0xe9edf1, 0xdde3e8, 0xb9c2cc};

constexpr uint32_t kOwnColour = 0x3b82f6;
constexpr uint32_t kGhatColour = 0xa78bfa;
constexpr uint32_t kGhatZoneColour = 0x6d5bb8;  // the hairpin on the map
constexpr uint32_t kGreen = 0x43d17a;
constexpr uint32_t kAmber = 0xf2b84b;
constexpr uint32_t kRed = 0xff5c63;

const Palette *pal = &kDark;
bool lightMode = false;

lv_color_t col(uint32_t hex) { return lv_color_hex(hex); }

// LVGL's built-in fonts have no middle dot (·), but do have a bullet (•)
std::string screenText(std::string s) {
  const std::string dot = "\xC2\xB7", bullet = "\xE2\x80\xA2";
  for (size_t i = s.find(dot); i != std::string::npos; i = s.find(dot, i + bullet.size())) {
    s.replace(i, dot.size(), bullet);
  }
  return s;
}

uint32_t typeColour(const std::string &type) {
  if (type == "bus") return 0xe0b84a;
  if (type == "lorry") return 0xc0773a;
  if (type == "truck") return 0xc58b4a;
  if (type == "auto") return 0x4cc38a;
  if (type == "priority") return 0xff6b6b;
  return 0x9aa7b4;  // car
}

const char *typeInitial(const std::string &type) {
  if (type == "bus") return "B";
  if (type == "lorry") return "L";
  if (type == "truck") return "T";
  if (type == "auto") return "A";
  if (type == "priority") return "P";
  return "C";
}

// ── State shown on screen ───────────────────────────────────────────────────
Fleet fleet;
AlertView view;
bool haveFleet = false;
bool serverOnline = false;
std::string problem = "Starting";
bool cameraFresh = false;
float cameraWidth = 0;
bool cameraWide = false;
bool flashOn = true;
uint32_t lastFlashMs = 0;
uint32_t lastClockMs = 0;

// ── Widgets ─────────────────────────────────────────────────────────────────
lv_obj_t *screen, *topBar, *titleLabel, *serverLabel, *gpsLabel, *clockLabel, *themeButton,
    *themeLabel;
lv_obj_t *mapObj, *overlay, *overlayLabel;
lv_obj_t *card, *cardIcon, *cardTitle, *cardDetail, *cardNote, *cardScene, *cardSteps;
lv_obj_t *stepLabels[3];
lv_obj_t *panel, *compassObj, *headingLabel, *roadLabel, *inRoadTitle, *inRoadLabel,
    *mountainBox, *mountainTitle, *mountainRoad, *mountainBend, *cameraLabel;
std::vector<lv_obj_t *> panelTitles;

// ── Drawing helpers ─────────────────────────────────────────────────────────
lv_coord_t clampCoord(double v) {
  return static_cast<lv_coord_t>(std::max(-3000.0, std::min(3000.0, v)));
}

void drawLine(lv_draw_ctx_t *ctx, lv_coord_t x1, lv_coord_t y1, lv_coord_t x2, lv_coord_t y2,
              uint32_t colour, lv_coord_t width, lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = col(colour);
  d.width = width;
  d.opa = opa;
  d.round_start = 1;
  d.round_end = 1;
  lv_point_t a = {x1, y1}, b = {x2, y2};
  lv_draw_line(ctx, &d, &a, &b);
}

void drawCircle(lv_draw_ctx_t *ctx, lv_coord_t cx, lv_coord_t cy, lv_coord_t r, uint32_t fill,
                uint32_t border = 0, lv_coord_t borderW = 0, lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.radius = LV_RADIUS_CIRCLE;
  d.bg_color = col(fill);
  d.bg_opa = opa;
  d.border_color = col(border);
  d.border_width = borderW;
  d.border_opa = borderW ? opa : LV_OPA_TRANSP;
  lv_area_t a = {static_cast<lv_coord_t>(cx - r), static_cast<lv_coord_t>(cy - r),
                 static_cast<lv_coord_t>(cx + r), static_cast<lv_coord_t>(cy + r)};
  lv_draw_rect(ctx, &d, &a);
}

void drawRect(lv_draw_ctx_t *ctx, lv_coord_t x1, lv_coord_t y1, lv_coord_t x2, lv_coord_t y2,
              uint32_t fill, lv_coord_t radius, uint32_t border = 0, lv_coord_t borderW = 0,
              lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.radius = radius;
  d.bg_color = col(fill);
  d.bg_opa = opa;
  d.border_color = col(border);
  d.border_width = borderW;
  d.border_opa = borderW ? LV_OPA_COVER : LV_OPA_TRANSP;
  lv_area_t a = {x1, y1, x2, y2};
  lv_draw_rect(ctx, &d, &a);
}

// Only convex shapes: triangles and the like
void drawPolygon(lv_draw_ctx_t *ctx, const lv_point_t *pts, uint16_t n, uint32_t fill,
                 lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = col(fill);
  d.bg_opa = opa;
  lv_draw_polygon(ctx, &d, pts, n);
}

void drawText(lv_draw_ctx_t *ctx, lv_coord_t x, lv_coord_t y, lv_coord_t w, const char *text,
              uint32_t colour, const lv_font_t *font, lv_text_align_t align = LV_TEXT_ALIGN_CENTER) {
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.color = col(colour);
  d.font = font;
  d.align = align;
  lv_area_t a = {x, y, static_cast<lv_coord_t>(x + w), static_cast<lv_coord_t>(y + font->line_height)};
  lv_draw_label(ctx, &d, &a, text, nullptr);
}

// An arrow pointing in compass direction `deg`, centred on (cx, cy)
void drawArrow(lv_draw_ctx_t *ctx, lv_coord_t cx, lv_coord_t cy, float deg, float size,
               uint32_t colour, lv_opa_t opa = LV_OPA_COVER) {
  float r = deg * static_cast<float>(M_PI) / 180.0f;
  float s = sinf(r), c = cosf(r);
  // Tip, back-right, notch, back-left; drawn as two convex triangles
  auto pt = [&](float fwd, float side) {
    lv_point_t p;
    p.x = clampCoord(cx + fwd * s + side * c);
    p.y = clampCoord(cy - fwd * c + side * s);
    return p;
  };
  lv_point_t tip = pt(size, 0), right = pt(-size * 0.7f, size * 0.7f),
             notch = pt(-size * 0.3f, 0), left = pt(-size * 0.7f, -size * 0.7f);
  lv_point_t a[3] = {tip, right, notch};
  lv_point_t b[3] = {tip, notch, left};
  drawPolygon(ctx, a, 3, colour, opa);
  drawPolygon(ctx, b, 3, colour, opa);
}

// ── The map ─────────────────────────────────────────────────────────────────
struct MapView {
  double lat0, lon0, mPerDegLon;
  float scale;
  lv_coord_t cx, cy;

  lv_point_t toScreen(double lat, double lon) const {
    double x = (lon - lon0) * mPerDegLon;
    double y = (lat - lat0) * 110540.0;
    lv_point_t p;
    p.x = clampCoord(cx + x * scale);
    p.y = clampCoord(cy - y * scale);
    return p;
  }
};

void drawPath(lv_draw_ctx_t *ctx, const MapView &mv, const Path &path, bool closed,
              uint32_t colour, lv_coord_t width, lv_opa_t opa = LV_OPA_COVER) {
  if (path.size() < 2) return;
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = col(colour);
  d.width = width;
  d.opa = opa;
  d.round_start = 1;
  d.round_end = 1;
  size_t n = path.size();
  for (size_t i = 0; i + 1 < n + (closed ? 1 : 0); i++) {
    lv_point_t a = mv.toScreen(path[i].lat, path[i].lon);
    lv_point_t b = mv.toScreen(path[(i + 1) % n].lat, path[(i + 1) % n].lon);
    lv_draw_line(ctx, &d, &a, &b);
  }
}

void drawBadge(lv_draw_ctx_t *ctx, lv_coord_t x, lv_coord_t y, int number, uint32_t colour) {
  drawCircle(ctx, x, y, 11, colour, 0xffffff, 2);
  char text[8];
  snprintf(text, sizeof(text), "%d", number);
  drawText(ctx, x - 11, y - 8, 22, text, 0xffffff, &lv_font_montserrat_14);
}

void drawVehicle(lv_draw_ctx_t *ctx, lv_coord_t x, lv_coord_t y, const Vehicle &v, bool own) {
  uint32_t colour = own ? kOwnColour : typeColour(v.vehicleType);
  lv_opa_t opa = v.active ? LV_OPA_COVER : LV_OPA_40;
  float size = own ? 17 : 13;
  if (!own && v.inSameRoad) drawCircle(ctx, x, y, 19, colour, colour, 2, LV_OPA_20);
  drawArrow(ctx, x, y, travelDirection(v), size, colour, opa);
  if (!own) {
    drawText(ctx, x + 12, y + 6, 16, typeInitial(v.vehicleType), pal->text, &lv_font_montserrat_14,
             LV_TEXT_ALIGN_LEFT);
  }
}

void drawMap(lv_event_t *e) {
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_area_t area;
  lv_obj_get_coords(mapObj, &area);

  if (!haveFleet || !fleet.own.hasPosition) return;

  const OwnVehicle &own = fleet.own;
  const std::string &kind = own.alert.kind;
  // Close in at narrow roads; wider on ghat roads, so the next hairpin and
  // the waiting point stay on screen
  bool close = !own.currentRoads.empty() || own.hasEntranceRoad || isInsideRoadAlert(kind) ||
               kind.rfind("ENTRY_", 0) == 0;
  bool ghat = own.mountain.active || kind.rfind("BEND_", 0) == 0;

  MapView mv;
  mv.lat0 = own.lat;
  mv.lon0 = own.lon;
  mv.mPerDegLon = 111320.0 * cos(own.lat * M_PI / 180.0);
  mv.scale = close ? MAP_SCALE_CLOSE : ghat ? MAP_SCALE_GHAT : MAP_SCALE_NORMAL;
  mv.cx = area.x1 + kMapW / 2;
  // Lower when a card covers the top, so the road around the car stays visible
  mv.cy = area.y1 + (view.show ? kMapH * 2 / 3 : kMapH / 2);

  // A light grid every 50 m, to show movement
  float gridStep = 50.0f * mv.scale;
  if (gridStep >= 40) {
    double offX = fmod((own.lon * mv.mPerDegLon) * mv.scale, gridStep);
    double offY = fmod((own.lat * 110540.0) * mv.scale, gridStep);
    for (float x = mv.cx - offX - gridStep * 8; x < area.x2; x += gridStep) {
      if (x > area.x1) drawLine(ctx, x, area.y1, x, area.y2, pal->mapGrid, 1);
    }
    for (float y = mv.cy + offY - gridStep * 8; y < area.y2; y += gridStep) {
      if (y > area.y1) drawLine(ctx, area.x1, y, area.x2, y, pal->mapGrid, 1);
    }
  }

  // At an entrance: the road ahead, coloured by whether it's free
  if (own.hasEntranceRoad) {
    uint32_t c = roadStatusColour(own.entranceStatus);
    drawPath(ctx, mv, own.entranceRoad, true, c, 5);
    lv_point_t p = mv.toScreen(own.entrancePoint.lat, own.entrancePoint.lon);
    drawCircle(ctx, p.x, p.y, 8, 0xffffff, c, 3);
  }

  // Inside a narrow road: its outline in the alert's colour
  uint32_t outline = view.show ? toneColour(view.tone) : kAmber;
  for (const Path &road : own.currentRoads) drawPath(ctx, mv, road, true, outline, 4);

  // Mountain Mode: the next hairpin, and where to wait
  if (own.mountain.active && own.mountain.hasNextBend) {
    drawPath(ctx, mv, own.mountain.nextBendZone, false, kGhatZoneColour, 10);
  }
  if (own.alert.hasWaitPoint) {
    lv_point_t p = mv.toScreen(own.alert.waitPoint.lat, own.alert.waitPoint.lon);
    drawCircle(ctx, p.x, p.y, 9, kRed, 0xffffff, 3);
    drawText(ctx, p.x - 50, p.y + 12, 100, "Wait here", kRed, &lv_font_montserrat_14);
  }

  // Other vehicles, with their places in the queue
  for (const Vehicle &v : fleet.others) {
    if (!v.hasPosition) continue;
    lv_point_t p = mv.toScreen(v.lat, v.lon);
    drawVehicle(ctx, p.x, p.y, v, false);
    if (v.queuePosition) drawBadge(ctx, p.x + 14, p.y - 16, v.queuePosition, 0x56606c);
  }

  drawVehicle(ctx, mv.cx, mv.cy, own, true);
  if (own.alert.queuePosition) drawBadge(ctx, mv.cx + 16, mv.cy - 18, own.alert.queuePosition, kOwnColour);

  // North, and a scale bar
  drawText(ctx, area.x1 + 10, area.y2 - 52, 30, "N", pal->muted, &lv_font_montserrat_16);
  drawArrow(ctx, area.x1 + 25, area.y2 - 64, 0, 7, pal->muted);
  float barM = close ? 10 : ghat ? 25 : 50;
  lv_coord_t barPx = barM * mv.scale;
  drawLine(ctx, area.x1 + 50, area.y2 - 20, area.x1 + 50 + barPx, area.y2 - 20, pal->muted, 3);
  char barText[16];
  snprintf(barText, sizeof(barText), "%.0f m", barM);
  drawText(ctx, area.x1 + 50, area.y2 - 42, barPx, barText, pal->muted, &lv_font_montserrat_14);
}

// ── The alert card's icon and road picture ──────────────────────────────────
void drawCardIcon(lv_event_t *e) {
  if (!view.show) return;
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_area_t a;
  lv_obj_get_coords(cardIcon, &a);
  lv_coord_t cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
  uint32_t c = toneColour(view.tone);

  switch (view.icon) {
    case AlertIcon::Yield: {
      lv_point_t outer[3] = {{static_cast<lv_coord_t>(cx - 28), static_cast<lv_coord_t>(cy - 22)},
                             {static_cast<lv_coord_t>(cx + 28), static_cast<lv_coord_t>(cy - 22)},
                             {cx, static_cast<lv_coord_t>(cy + 26)}};
      lv_point_t inner[3] = {{static_cast<lv_coord_t>(cx - 16), static_cast<lv_coord_t>(cy - 15)},
                             {static_cast<lv_coord_t>(cx + 16), static_cast<lv_coord_t>(cy - 15)},
                             {cx, static_cast<lv_coord_t>(cy + 12)}};
      drawPolygon(ctx, outer, 3, c);
      drawPolygon(ctx, inner, 3, 0xffffff);
      break;
    }
    case AlertIcon::ArrowUp:
      drawArrow(ctx, cx, cy, 0, 26, c);
      break;
    case AlertIcon::Check:
      drawLine(ctx, cx - 20, cy, cx - 6, cy + 16, c, 8);
      drawLine(ctx, cx - 6, cy + 16, cx + 22, cy - 16, c, 8);
      break;
    case AlertIcon::Car:
      drawRect(ctx, cx - 26, cy - 10, cx + 26, cy + 12, c, 6);
      drawRect(ctx, cx - 15, cy - 22, cx + 15, cy - 6, c, 6);
      drawCircle(ctx, cx - 14, cy + 14, 6, pal->surface, c, 3);
      drawCircle(ctx, cx + 14, cy + 14, 6, pal->surface, c, 3);
      break;
    case AlertIcon::Road:
      drawLine(ctx, cx - 10, cy - 24, cx - 22, cy + 24, c, 5);
      drawLine(ctx, cx + 10, cy - 24, cx + 22, cy + 24, c, 5);
      drawLine(ctx, cx, cy - 18, cx, cy - 8, c, 4);
      drawLine(ctx, cx, cy + 4, cx, cy + 16, c, 4);
      break;
    case AlertIcon::Warning: {
      lv_point_t tri[3] = {{cx, static_cast<lv_coord_t>(cy - 26)},
                           {static_cast<lv_coord_t>(cx + 28), static_cast<lv_coord_t>(cy + 22)},
                           {static_cast<lv_coord_t>(cx - 28), static_cast<lv_coord_t>(cy + 22)}};
      drawPolygon(ctx, tri, 3, c);
      drawText(ctx, cx - 10, cy - 10, 20, "!", 0x000000, &lv_font_montserrat_28);
      break;
    }
  }
}

void drawSceneVehicle(lv_draw_ctx_t *ctx, lv_coord_t x, lv_coord_t y, bool facingRight,
                      uint32_t colour, bool highlight) {
  if (highlight) drawRect(ctx, x - 34, y - 16, x + 34, y + 16, colour, 8, colour, 0, LV_OPA_30);
  drawRect(ctx, x - 26, y - 10, x + 26, y + 10, colour, 5);
  lv_coord_t nose = facingRight ? x + 20 : x - 20;
  drawRect(ctx, nose - 4, y - 7, nose + 4, y + 7, 0xffffff, 2, 0, 0, LV_OPA_60);
}

void drawScene(lv_event_t *e) {
  if (!view.show || !view.scene.show) return;
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_area_t a;
  lv_obj_get_coords(cardScene, &a);
  const AlertScene &s = view.scene;
  uint32_t tone = toneColour(view.tone);
  lv_coord_t w = a.x2 - a.x1, mid = a.y1 + 22;

  // The road: starts after the other vehicle when it's still outside
  lv_coord_t roadStart = s.outside ? a.x1 + 110 : a.x1;
  drawRect(ctx, roadStart, mid - 16, a.x2, mid + 16, pal->road, 6);

  lv_coord_t otherX = a.x1 + 60, ownX = a.x2 - 60;
  std::string otherType = fleet.own.alert.otherVehicleType;
  drawSceneVehicle(ctx, otherX, mid, true, typeColour(otherType), s.goes < 0);
  drawSceneVehicle(ctx, ownX, mid, false, kOwnColour, s.goes > 0);
  if (fleet.own.alert.otherCount > 1) {
    char more[8];
    snprintf(more, sizeof(more), "+%d", fleet.own.alert.otherCount - 1);
    drawText(ctx, otherX + 30, mid - 30, 40, more, pal->text, &lv_font_montserrat_14);
  }

  // Chevrons showing who moves
  if (s.goes != 0) {
    for (int i = 0; i < 3; i++) {
      lv_coord_t x = a.x1 + w / 2 - 40 + i * 40;
      int dir = s.goes < 0 ? 1 : -1;  // the other vehicle goes: move right
      drawLine(ctx, x - 8 * dir, mid - 9, x + 6 * dir, mid, tone, 4);
      drawLine(ctx, x + 6 * dir, mid, x - 8 * dir, mid + 9, tone, 4);
    }
  }
  if (s.stopLine) drawLine(ctx, ownX - 42, mid - 18, ownX - 42, mid + 18, kRed, 5);

  std::string otherName = vehicleLabel(otherType);
  if (otherType == "priority") otherName = "Priority";
  if (otherName.empty()) otherName = "Vehicle";
  std::string otherText = screenText(otherName + " · " + s.otherLabel);
  std::string ownText = screenText("You · " + s.ownLabel);
  drawText(ctx, a.x1, mid + 22, 200, otherText.c_str(), pal->text, &lv_font_montserrat_14,
           LV_TEXT_ALIGN_LEFT);
  drawText(ctx, a.x2 - 200, mid + 22, 200, ownText.c_str(), pal->text, &lv_font_montserrat_14,
           LV_TEXT_ALIGN_RIGHT);
}

// ── The side panel's compass ────────────────────────────────────────────────
const char *compassName(float deg) {
  static const char *names[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int i = static_cast<int>(floorf(fmodf(deg + 22.5f + 360.0f, 360.0f) / 45.0f)) % 8;
  return names[i];
}

void drawCompass(lv_event_t *e) {
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_area_t a;
  lv_obj_get_coords(compassObj, &a);
  lv_coord_t cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
  drawCircle(ctx, cx, cy, 30, pal->surface, pal->border, 2);
  drawText(ctx, cx - 10, cy - 30, 20, "N", pal->muted, &lv_font_montserrat_14);
  bool known = haveFleet && fleet.own.active && fleet.own.hasPosition;
  if (known) drawArrow(ctx, cx, cy, travelDirection(fleet.own), 19, kOwnColour);
}

// ── Building widgets ────────────────────────────────────────────────────────
lv_obj_t *plainBox(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, const char *text = "") {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_label_set_text(l, text);
  return l;
}

lv_obj_t *panelTitle(const char *text) {
  lv_obj_t *t = label(panel, &lv_font_montserrat_14, text);
  panelTitles.push_back(t);
  return t;
}

void applyTheme() {
  pal = lightMode ? &kLight : &kDark;
  lv_obj_set_style_bg_color(screen, col(pal->background), 0);
  lv_obj_set_style_bg_color(topBar, col(pal->surface), 0);
  lv_obj_set_style_border_color(topBar, col(pal->border), 0);
  lv_obj_set_style_bg_color(mapObj, col(pal->mapBackground), 0);
  lv_obj_set_style_bg_color(panel, col(pal->surface), 0);
  lv_obj_set_style_border_color(panel, col(pal->border), 0);
  lv_obj_set_style_bg_color(card, col(pal->surface), 0);
  lv_obj_set_style_bg_color(overlay, col(pal->surface), 0);
  lv_obj_set_style_text_color(screen, col(pal->text), 0);
  for (lv_obj_t *t : panelTitles) lv_obj_set_style_text_color(t, col(pal->muted), 0);
  lv_obj_set_style_text_color(cardNote, col(pal->muted), 0);
  lv_label_set_text(themeLabel, lightMode ? "Dark" : "Light");
  lv_obj_invalidate(screen);
}

void onThemeButton(lv_event_t *) {
  lightMode = !lightMode;
  Preferences prefs;
  prefs.begin("screen", false);
  prefs.putBool("light", lightMode);
  prefs.end();
  applyTheme();
}

void setStatusLabel(lv_obj_t *l, const char *text, uint32_t colour) {
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, col(colour), 0);
}

void refreshPanel() {
  const OwnVehicle &own = fleet.own;
  bool live = haveFleet && own.active && own.hasPosition;

  if (live) {
    float d = travelDirection(own);
    lv_label_set_text_fmt(headingLabel, "%s %d°", compassName(d), static_cast<int>(lroundf(d)) % 360);
  } else {
    lv_label_set_text(headingLabel, "--");
  }

  bool inside = isInsideRoadAlert(own.alert.kind);
  if (!live) {
    lv_label_set_text(roadLabel, "--");
  } else if (inside) {
    lv_label_set_text(roadLabel, hasRoadName(own.alert.roadName) ? own.alert.roadName.c_str()
                                                                 : "Narrow road");
  } else {
    lv_label_set_text(roadLabel, own.mountain.active ? "Ghat road" : "Regular road");
  }

  // Vehicles in the same narrow road, nearest first
  std::vector<std::pair<double, std::string>> inRoad;
  if (live && inside) {
    for (const Vehicle &v : fleet.others) {
      if (!v.active || !v.inSameRoad || !v.hasPosition) continue;
      double dx = (v.lon - own.lon) * 111320.0 * cos(own.lat * M_PI / 180.0);
      double dy = (v.lat - own.lat) * 110540.0;
      double dist = sqrt(dx * dx + dy * dy);
      std::string label = vehicleLabel(v.vehicleType);
      if (label.empty()) label = "Vehicle";
      char line[64];
      snprintf(line, sizeof(line), "%s \xE2\x80\xA2 %s \xE2\x80\xA2 %.0f m", label.c_str(),
               isMoving(v) ? "moving" : "stopped", dist);
      inRoad.push_back({dist, line});
    }
  }
  std::sort(inRoad.begin(), inRoad.end());
  if (inside) {
    lv_obj_clear_flag(inRoadTitle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(inRoadLabel, LV_OBJ_FLAG_HIDDEN);
    std::string text = inRoad.empty() ? "No other vehicles" : "";
    for (size_t i = 0; i < inRoad.size() && i < 4; i++) {
      text += (i ? "\n" : "") + inRoad[i].second;
    }
    lv_label_set_text(inRoadLabel, screenText(text).c_str());
  } else {
    lv_obj_add_flag(inRoadTitle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(inRoadLabel, LV_OBJ_FLAG_HIDDEN);
  }

  // Mountain Mode: "Hairpin 5 of 18 in 90 m"
  const MountainInfo &m = own.mountain;
  if (live && m.active) {
    lv_obj_clear_flag(mountainBox, LV_OBJ_FLAG_HIDDEN);
    std::string title = "MOUNTAIN MODE";
    if (m.heading == "UP") title += " · UPHILL";
    if (m.heading == "DOWN") title += " · DOWNHILL";
    lv_label_set_text(mountainTitle, screenText(title).c_str());
    lv_label_set_text(mountainRoad, m.roadName.c_str());
    if (m.inBend) {
      lv_label_set_text_fmt(mountainBend, "In hairpin %d of %d", m.inBend, m.bendCount);
    } else if (m.hasNextBend) {
      lv_label_set_text_fmt(mountainBend, "Hairpin %d of %d in %d m", m.nextBendNumber, m.bendCount,
                            m.nextBendDistanceM);
    } else {
      lv_label_set_text_fmt(mountainBend, "%d hairpins", m.bendCount);
    }
  } else {
    lv_obj_add_flag(mountainBox, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_invalidate(compassObj);
}

void refreshCamera() {
  if (!cameraFresh) {
    lv_label_set_text(cameraLabel, "No camera reading");
    lv_obj_set_style_text_color(cameraLabel, col(pal->muted), 0);
  } else if (cameraWide) {
    lv_label_set_text_fmt(cameraLabel, "Wider than %.0f m", cameraWidth);
    lv_obj_set_style_text_color(cameraLabel, col(kGreen), 0);
  } else {
    bool narrow = cameraWidth <= NARROW_MAX_WIDTH_M;
    lv_label_set_text_fmt(cameraLabel, "%.1f m%s", cameraWidth, narrow ? " \xE2\x80\xA2 narrow" : "");
    lv_obj_set_style_text_color(cameraLabel, col(narrow ? kAmber : pal->text), 0);
  }
}

void refreshCard() {
  if (!view.show) {
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);
  uint32_t tone = toneColour(view.tone);
  lv_obj_set_style_border_color(card, col(tone), 0);
  lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(cardTitle, col(tone), 0);
  lv_label_set_text(cardTitle, screenText(view.title).c_str());
  lv_label_set_text(cardDetail, screenText(view.detail).c_str());
  lv_label_set_text(cardNote, screenText(view.note).c_str());
  if (view.note.empty()) lv_obj_add_flag(cardNote, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(cardNote, LV_OBJ_FLAG_HIDDEN);

  if (view.scene.show) lv_obj_clear_flag(cardScene, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(cardScene, LV_OBJ_FLAG_HIDDEN);

  if (view.step >= 0) {
    lv_obj_clear_flag(cardSteps, LV_OBJ_FLAG_HIDDEN);
    static const char *names[] = {"Stop", "Let it pass", "Continue"};
    for (int i = 0; i < 3; i++) {
      bool done = i < view.step, current = i == view.step;
      if (done) lv_label_set_text_fmt(stepLabels[i], LV_SYMBOL_OK " %s", names[i]);
      else lv_label_set_text_fmt(stepLabels[i], "%d  %s", i + 1, names[i]);
      lv_obj_set_style_bg_opa(stepLabels[i], current ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
      lv_obj_set_style_bg_color(stepLabels[i], col(tone), 0);
      lv_obj_set_style_text_color(stepLabels[i], col(current ? 0x000000 : (done ? tone : pal->muted)), 0);
    }
  } else {
    lv_obj_add_flag(cardSteps, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_invalidate(cardIcon);
  lv_obj_invalidate(cardScene);
}

void refreshStatus() {
  setStatusLabel(serverLabel, serverOnline ? LV_SYMBOL_WIFI " Online" : LV_SYMBOL_REFRESH " Reconnecting",
                 serverOnline ? kGreen : kAmber);

  const OwnVehicle &own = fleet.own;
  if (!haveFleet || !own.active) setStatusLabel(gpsLabel, LV_SYMBOL_GPS " No GPS", pal->muted);
  else if (own.weakSignal) setStatusLabel(gpsLabel, LV_SYMBOL_GPS " Weak signal", kAmber);
  else setStatusLabel(gpsLabel, LV_SYMBOL_GPS " Live", kGreen);

  // Why there's no car on the map
  std::string reason = problem;
  if (reason.empty() && haveFleet && !own.active) {
    reason = "No position from the main unit yet.\nIt needs a GPS fix: a clear view of the sky helps.";
  }
  if (reason.empty()) {
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_label_set_text(overlayLabel, reason.c_str());
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  }
}

}  // namespace

void uiBegin() {
  Preferences prefs;
  prefs.begin("screen", true);
  lightMode = prefs.getBool("light", false);
  prefs.end();

  screen = lv_scr_act();
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_font(screen, &lv_font_montserrat_16, 0);

  // Top bar
  topBar = plainBox(screen);
  lv_obj_set_size(topBar, LCD_WIDTH, kTopBarH);
  lv_obj_set_style_bg_opa(topBar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(topBar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(topBar, 1, 0);

  titleLabel = label(topBar, &lv_font_montserrat_16, screenText("Narrow Road · " ACCOUNT_USERNAME).c_str());
  lv_obj_align(titleLabel, LV_ALIGN_LEFT_MID, 12, 0);

  themeButton = lv_btn_create(topBar);
  lv_obj_set_size(themeButton, 70, 32);
  lv_obj_align(themeButton, LV_ALIGN_RIGHT_MID, -6, 0);
  lv_obj_add_event_cb(themeButton, onThemeButton, LV_EVENT_CLICKED, nullptr);
  themeLabel = label(themeButton, &lv_font_montserrat_14, "Light");
  lv_obj_center(themeLabel);

  clockLabel = label(topBar, &lv_font_montserrat_20, "--:--");
  lv_obj_align(clockLabel, LV_ALIGN_RIGHT_MID, -90, 0);
  gpsLabel = label(topBar, &lv_font_montserrat_14);
  lv_obj_align(gpsLabel, LV_ALIGN_RIGHT_MID, -170, 0);
  serverLabel = label(topBar, &lv_font_montserrat_14);
  lv_obj_align(serverLabel, LV_ALIGN_RIGHT_MID, -300, 0);

  // Map
  mapObj = plainBox(screen);
  lv_obj_set_pos(mapObj, 0, kTopBarH);
  lv_obj_set_size(mapObj, kMapW, kMapH);
  lv_obj_set_style_bg_opa(mapObj, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(mapObj, drawMap, LV_EVENT_DRAW_MAIN, nullptr);

  // Message over the map when there's no car to show
  overlay = plainBox(mapObj);
  lv_obj_set_size(overlay, kMapW - 80, LV_SIZE_CONTENT);
  lv_obj_center(overlay);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_90, 0);
  lv_obj_set_style_radius(overlay, 12, 0);
  lv_obj_set_style_pad_all(overlay, 20, 0);
  overlayLabel = label(overlay, &lv_font_montserrat_20);
  lv_obj_set_width(overlayLabel, LV_PCT(100));
  lv_label_set_long_mode(overlayLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(overlayLabel, LV_TEXT_ALIGN_CENTER, 0);

  // Alert card
  card = plainBox(screen);
  lv_obj_set_pos(card, kCardMargin, kTopBarH + kCardMargin);
  lv_obj_set_size(card, kCardW, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 14, 0);
  lv_obj_set_style_border_width(card, 4, 0);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_set_style_pad_row(card, 8, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *head = plainBox(card);
  lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(head, 12, 0);
  cardIcon = plainBox(head);
  lv_obj_set_size(cardIcon, 64, 64);
  lv_obj_add_event_cb(cardIcon, drawCardIcon, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_t *texts = plainBox(head);
  lv_obj_set_size(texts, kCardW - 24 - 64 - 12, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(texts, 2, 0);
  cardTitle = label(texts, &lv_font_montserrat_28);
  cardDetail = label(texts, &lv_font_montserrat_20);
  cardNote = label(texts, &lv_font_montserrat_16);
  for (lv_obj_t *l : {cardTitle, cardDetail, cardNote}) {
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  }

  cardScene = plainBox(card);
  lv_obj_set_size(cardScene, LV_PCT(100), 64);
  lv_obj_add_event_cb(cardScene, drawScene, LV_EVENT_DRAW_MAIN, nullptr);

  cardSteps = plainBox(card);
  lv_obj_set_size(cardSteps, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(cardSteps, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(cardSteps, 8, 0);
  for (int i = 0; i < 3; i++) {
    stepLabels[i] = label(cardSteps, &lv_font_montserrat_16);
    lv_obj_set_style_pad_hor(stepLabels[i], 10, 0);
    lv_obj_set_style_pad_ver(stepLabels[i], 4, 0);
    lv_obj_set_style_radius(stepLabels[i], 12, 0);
  }

  // Side panel
  panel = plainBox(screen);
  lv_obj_set_pos(panel, kMapW, kTopBarH);
  lv_obj_set_size(panel, kPanelW, kMapH);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_pad_all(panel, 14, 0);
  lv_obj_set_style_pad_row(panel, 6, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

  panelTitle("HEADING");
  lv_obj_t *headingRow = plainBox(panel);
  lv_obj_set_size(headingRow, LV_PCT(100), 66);
  compassObj = plainBox(headingRow);
  lv_obj_set_size(compassObj, 66, 66);
  lv_obj_add_event_cb(compassObj, drawCompass, LV_EVENT_DRAW_MAIN, nullptr);
  headingLabel = label(headingRow, &lv_font_montserrat_28, "--");
  lv_obj_align(headingLabel, LV_ALIGN_RIGHT_MID, 0, 0);

  panelTitle("ROAD");
  roadLabel = label(panel, &lv_font_montserrat_20, "--");
  lv_obj_set_width(roadLabel, LV_PCT(100));
  lv_label_set_long_mode(roadLabel, LV_LABEL_LONG_WRAP);

  inRoadTitle = panelTitle("IN THIS ROAD");
  inRoadLabel = label(panel, &lv_font_montserrat_14);
  lv_obj_set_width(inRoadLabel, LV_PCT(100));

  mountainBox = plainBox(panel);
  lv_obj_set_size(mountainBox, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(mountainBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(mountainBox, 2, 0);
  lv_obj_set_style_pad_top(mountainBox, 6, 0);
  mountainTitle = label(mountainBox, &lv_font_montserrat_14, "MOUNTAIN MODE");
  lv_obj_set_width(mountainTitle, LV_PCT(100));
  lv_label_set_long_mode(mountainTitle, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(mountainTitle, col(kGhatColour), 0);
  mountainRoad = label(mountainBox, &lv_font_montserrat_16);
  lv_obj_set_width(mountainRoad, LV_PCT(100));
  lv_label_set_long_mode(mountainRoad, LV_LABEL_LONG_WRAP);
  mountainBend = label(mountainBox, &lv_font_montserrat_16);

  panelTitle("ROAD AHEAD (CAMERA)");
  cameraLabel = label(panel, &lv_font_montserrat_16);

  applyTheme();
  refreshPanel();
  refreshCamera();
  refreshCard();
  refreshStatus();
}

void uiShowFleet(const Fleet &f) {
  fleet = f;
  haveFleet = f.hasOwn;
  view = (haveFleet && fleet.own.active) ? describeAlert(fleet.own.alert, fleet.own) : AlertView();
  refreshCard();
  refreshPanel();
  refreshStatus();
  lv_obj_invalidate(mapObj);
}

void uiShowStatus(bool online, const std::string &p) {
  if (online == serverOnline && p == problem) return;
  serverOnline = online;
  problem = p;
  if (!online) {
    haveFleet = false;
    view = AlertView();
    refreshCard();
    refreshPanel();
    lv_obj_invalidate(mapObj);
  }
  refreshStatus();
}

void uiShowCamera(bool fresh, float widthM, bool wide) {
  if (fresh == cameraFresh && fabsf(widthM - cameraWidth) < 0.05f && wide == cameraWide) return;
  cameraFresh = fresh;
  cameraWidth = widthM;
  cameraWide = wide;
  refreshCamera();
}

void uiTick() {
  uint32_t now = millis();

  // Danger alerts flash, as there's no sound
  if (view.show && view.tone == Tone::Stop && now - lastFlashMs >= ALERT_FLASH_MS) {
    lastFlashMs = now;
    flashOn = !flashOn;
    lv_obj_set_style_border_opa(card, flashOn ? LV_OPA_COVER : LV_OPA_20, 0);
  }

  if (now - lastClockMs >= 1000) {
    lastClockMs = now;
    struct tm t;
    if (getLocalTime(&t, 0)) lv_label_set_text_fmt(clockLabel, "%02d:%02d", t.tm_hour, t.tm_min);
  }
}
