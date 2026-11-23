// Narrow Road Navigation: camera unit on an ESP32-CAM (AI-Thinker)
//
// Measure mode: takes a 320x240 grayscale picture about five times a second,
// measures the road's width (width_core.cpp), and broadcasts it on the
// hotspot for the main unit and screen:
//   {"type":"width","width":3.2,"wide":false,"distance":6.0,"confidence":0.8}
// It also serves a small web page at its address, with /frame.bmp (the
// picture, for calibrating) and /overlay.bmp (the picture with the
// measurement marked).
//
// Stream mode: sends video at :81/stream for width.py to measure instead.
//
// Settings: include/config.h and include/secrets.h

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_camera.h>
#include <esp_http_server.h>

#include "config.h"
#include "width_core.h"

#if defined(CAMERA_MODE_MEASURE) == defined(CAMERA_MODE_STREAM)
#error "Choose one of CAMERA_MODE_MEASURE or CAMERA_MODE_STREAM in config.h"
#endif

namespace {

// AI-Thinker ESP32-CAM camera pins
constexpr int PWDN = 32, RESET = -1, XCLK = 0, SIOD = 26, SIOC = 27;
constexpr int Y9 = 35, Y8 = 34, Y7 = 39, Y6 = 36, Y5 = 21, Y4 = 19, Y3 = 18, Y2 = 5;
constexpr int VSYNC = 25, HREF = 23, PCLK = 22;
constexpr int FLASH_LED = 4;

constexpr int kWidth = 320, kHeight = 240;

WiFiUDP udp;
httpd_handle_t web = nullptr;
bool wifiAnnounced = false;

bool startCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2;
  c.pin_d1 = Y3;
  c.pin_d2 = Y4;
  c.pin_d3 = Y5;
  c.pin_d4 = Y6;
  c.pin_d5 = Y7;
  c.pin_d6 = Y8;
  c.pin_d7 = Y9;
  c.pin_xclk = XCLK;
  c.pin_pclk = PCLK;
  c.pin_vsync = VSYNC;
  c.pin_href = HREF;
  c.pin_sccb_sda = SIOD;
  c.pin_sccb_scl = SIOC;
  c.pin_pwdn = PWDN;
  c.pin_reset = RESET;
  c.xclk_freq_hz = 20000000;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;  // always the newest picture
  c.fb_count = 2;
#ifdef CAMERA_MODE_MEASURE
  c.pixel_format = PIXFORMAT_GRAYSCALE;
  c.frame_size = FRAMESIZE_QVGA;  // 320x240, what the calibration is for
#else
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_VGA;  // 640x480; width.py scales it to the calibration
  c.jpeg_quality = 12;
#endif

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("[Camera] Failed to start (0x%x). Check the ribbon cable.\n", err);
    return false;
  }
  sensor_t *s = esp_camera_sensor_get();
  s->set_vflip(s, CAMERA_FLIP_VERTICAL);
  s->set_hmirror(s, CAMERA_MIRROR);
  Serial.println("[Camera] Ready");
  return true;
}

#ifdef CAMERA_MODE_MEASURE

Homography toGround = {CAM_IMAGE_TO_GROUND};
Homography toImage;
WidthSettings settings;
WidthSmoother smoother;

// The latest picture and result, shared with the web pages
SemaphoreHandle_t lock;
uint8_t *lastFrame = nullptr;
uint8_t *pageFrame = nullptr;  // a copy for the web page to draw on
WidthResult lastRaw, lastSmoothed;
uint32_t lastMeasureMs = 0;
uint32_t readings = 0;

void broadcast(const WidthResult &r) {
  char msg[128];
  snprintf(msg, sizeof(msg),
           "{\"type\":\"width\",\"width\":%.2f,\"wide\":%s,\"distance\":%.1f,\"confidence\":%.2f}",
           r.width, r.wide ? "true" : "false", r.distance, r.confidence);
  udp.beginPacket(WiFi.broadcastIP(), CAMERA_UDP_PORT);
  udp.print(msg);
  udp.endPacket();
}

void measure() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;
  if (fb->width != kWidth || fb->height != kHeight) {
    esp_camera_fb_return(fb);
    return;
  }

  WidthResult raw = measureWidth(fb->buf, fb->width, fb->height, toGround, toImage, settings);
  WidthResult smoothed = smoother.add(raw);

  if (xSemaphoreTake(lock, 0) == pdTRUE) {
    memcpy(lastFrame, fb->buf, kWidth * kHeight);
    lastRaw = raw;
    lastSmoothed = smoothed;
    xSemaphoreGive(lock);
  }
  esp_camera_fb_return(fb);

  if (smoothed.valid && WiFi.status() == WL_CONNECTED) {
    broadcast(smoothed);
    readings++;
  }
}

// An 8-bit grayscale BMP, which any browser shows and calibrate.py reads
esp_err_t sendBmp(httpd_req_t *req, bool overlay) {
  uint8_t *frame = pageFrame;
  WidthResult r;
  xSemaphoreTake(lock, portMAX_DELAY);
  memcpy(frame, lastFrame, kWidth * kHeight);
  r = lastRaw;
  xSemaphoreGive(lock);

  if (overlay) {
    // Measured rows in white, sides in black, so they show on any picture
    for (const RowResult &row : r.rows) {
      if (!row.used) continue;
      for (int u = row.left; u <= row.right; u++) frame[row.v * kWidth + u] = 255;
      for (int dv = -4; dv <= 4; dv++) {
        int v = row.v + dv;
        if (v < 0 || v >= kHeight) continue;
        frame[v * kWidth + row.left] = 0;
        frame[v * kWidth + row.right] = 0;
        frame[v * kWidth + row.centre] = 255;
      }
    }
  }

  uint8_t header[54 + 1024] = {0};
  uint32_t dataSize = kWidth * kHeight, offset = sizeof(header), fileSize = offset + dataSize;
  header[0] = 'B';
  header[1] = 'M';
  memcpy(header + 2, &fileSize, 4);
  memcpy(header + 10, &offset, 4);
  uint32_t infoSize = 40;
  int32_t w = kWidth, h = kHeight;
  uint16_t planes = 1, bits = 8;
  uint32_t colours = 256;
  memcpy(header + 14, &infoSize, 4);
  memcpy(header + 18, &w, 4);
  memcpy(header + 22, &h, 4);
  memcpy(header + 26, &planes, 2);
  memcpy(header + 28, &bits, 2);
  memcpy(header + 34, &dataSize, 4);
  memcpy(header + 46, &colours, 4);
  for (int i = 0; i < 256; i++) {
    header[54 + i * 4] = header[55 + i * 4] = header[56 + i * 4] = i;
  }

  httpd_resp_set_type(req, "image/bmp");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_send_chunk(req, reinterpret_cast<const char *>(header), sizeof(header));
  for (int v = kHeight - 1; v >= 0; v--) {  // BMP rows go bottom to top
    httpd_resp_send_chunk(req, reinterpret_cast<const char *>(frame + v * kWidth), kWidth);
  }
  return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t frameHandler(httpd_req_t *req) { return sendBmp(req, false); }
esp_err_t overlayHandler(httpd_req_t *req) { return sendBmp(req, true); }

esp_err_t statusHandler(httpd_req_t *req) {
  WidthResult r;
  xSemaphoreTake(lock, portMAX_DELAY);
  r = lastSmoothed;
  xSemaphoreGive(lock);
  char json[160];
  snprintf(json, sizeof(json),
           "{\"valid\":%s,\"width\":%.2f,\"wide\":%s,\"confidence\":%.2f,\"readings\":%lu}",
           r.valid ? "true" : "false", r.width, r.wide ? "true" : "false", r.confidence,
           static_cast<unsigned long>(readings));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

const char kPage[] = R"HTML(<!doctype html><meta name=viewport content="width=device-width">
<title>Road camera</title><style>body{font:16px sans-serif;margin:16px;background:#111;color:#eee}
img{width:100%;max-width:640px;image-rendering:pixelated;border:1px solid #444}b{font-size:28px}</style>
<h2>Road camera</h2><p>Road width: <b id=w>...</b></p>
<img id=i src="/overlay.bmp"><p>White lines: where it measures. Black marks: the sides it found.
<a href="/frame.bmp" download="frame.bmp" style=color:#8cf>Save a picture for calibrating</a></p>
<script>setInterval(async()=>{try{const r=await(await fetch('/status.json')).json();
document.getElementById('w').textContent=!r.valid?'no reading':r.wide?'wider than '+r.width.toFixed(1)+' m':r.width.toFixed(2)+' m';
document.getElementById('i').src='/overlay.bmp?'+Date.now()}catch(e){}},1000)</script>)HTML";

esp_err_t pageHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kPage, HTTPD_RESP_USE_STRLEN);
}

void startWeb() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  if (httpd_start(&web, &config) != ESP_OK) return;
  httpd_uri_t pages[] = {
      {"/", HTTP_GET, pageHandler, nullptr},
      {"/frame.bmp", HTTP_GET, frameHandler, nullptr},
      {"/overlay.bmp", HTTP_GET, overlayHandler, nullptr},
      {"/status.json", HTTP_GET, statusHandler, nullptr},
  };
  for (auto &p : pages) httpd_register_uri_handler(web, &p);
}

#else  // CAMERA_MODE_STREAM

#define PART_BOUNDARY "123456789000000000000987654321"

esp_err_t streamHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" PART_BOUNDARY);
  char part[64];
  for (;;) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) continue;
    int n = snprintf(part, sizeof(part),
                     "\r\n--" PART_BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                     static_cast<unsigned>(fb->len));
    esp_err_t res = httpd_resp_send_chunk(req, part, n);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, reinterpret_cast<const char *>(fb->buf), fb->len);
    esp_camera_fb_return(fb);
    if (res != ESP_OK) return res;  // the viewer went away
  }
}

esp_err_t pageHandler(httpd_req_t *req) {
  static const char page[] =
      "<!doctype html><title>Road camera</title><body style='background:#111;color:#eee;font:16px sans-serif'>"
      "<p>Video for width.py: <code>http://this-address:81/stream</code></p>"
      "<img id=v style='max-width:100%'><script>v.src=location.origin.replace(/:\\d+$/,'')+':81/stream'</script>";
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

void startWeb() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;
  if (httpd_start(&web, &config) == ESP_OK) {
    httpd_uri_t page = {"/", HTTP_GET, pageHandler, nullptr};
    httpd_register_uri_handler(web, &page);
  }
  httpd_handle_t stream = nullptr;
  httpd_config_t streamConfig = HTTPD_DEFAULT_CONFIG();
  streamConfig.server_port = 81;
  streamConfig.ctrl_port = 32769;
  if (httpd_start(&stream, &streamConfig) == ESP_OK) {
    httpd_uri_t s = {"/stream", HTTP_GET, streamHandler, nullptr};
    httpd_register_uri_handler(stream, &s);
  }
}

#endif

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nNarrow Road Navigation: camera unit");

  pinMode(FLASH_LED, OUTPUT);
  digitalWrite(FLASH_LED, LOW);

  if (!psramFound()) Serial.println("[Camera] No PSRAM found; pictures may fail");

#ifdef CAMERA_MODE_MEASURE
  lock = xSemaphoreCreateMutex();
  lastFrame = static_cast<uint8_t *>(ps_malloc(kWidth * kHeight));
  memset(lastFrame, 0, kWidth * kHeight);
  pageFrame = static_cast<uint8_t *>(ps_malloc(kWidth * kHeight));
  if (!invertHomography(toGround, toImage)) Serial.println("[Camera] Bad calibration in config.h");
  float distances[] = MEASURE_DISTANCES;
  settings.distanceCount = 0;
  for (float d : distances) {
    if (settings.distanceCount < 5) settings.distances[settings.distanceCount++] = d;
  }
  settings.narrowMaxM = NARROW_MAX_WIDTH_M;
#endif

  while (!startCamera()) delay(2000);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // steadier video and readings
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  startWeb();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && !wifiAnnounced) {
    wifiAnnounced = true;
    Serial.printf("[Camera] On Wi-Fi. Open http://%s/ in a browser on the hotspot\n",
                  WiFi.localIP().toString().c_str());
  } else if (WiFi.status() != WL_CONNECTED) {
    wifiAnnounced = false;
  }

#ifdef CAMERA_MODE_MEASURE
  if (millis() - lastMeasureMs >= MEASURE_INTERVAL_MS) {
    lastMeasureMs = millis();
    measure();
  }
  static uint32_t lastLog = 0;
  if (millis() - lastLog > 2000) {
    lastLog = millis();
    if (lastSmoothed.valid) {
      Serial.printf("[Width] %s%.2f m (confidence %.2f)\n", lastSmoothed.wide ? ">" : "",
                    lastSmoothed.width, lastSmoothed.confidence);
    } else {
      Serial.println("[Width] no reading: is the camera seeing the road 5-7 m ahead?");
    }
  }
#endif
  delay(5);
}
