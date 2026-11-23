#include "gps_reader.h"

#include <math.h>

void GpsReader::begin(int rxPin, int txPin, uint32_t baud) {
  serial_.begin(baud, SERIAL_8N1, rxPin, txPin);
  delay(100);
  // u-blox: also send GST (accuracy) once per fix on UART1.
  // Other modules ignore this sentence.
  sendUbloxCommand("PUBX,40,GST,0,1,0,0,0,0");
}

void GpsReader::sendUbloxCommand(const char *body) {
  uint8_t checksum = 0;
  for (const char *p = body; *p; p++) checksum ^= static_cast<uint8_t>(*p);
  char line[64];
  snprintf(line, sizeof(line), "$%s*%02X\r\n", body, checksum);
  serial_.print(line);
}

void GpsReader::update() {
  while (serial_.available() > 0) {
    gps_.encode(serial_.read());
  }
}

bool GpsReader::hasFix(uint32_t maxAgeMs) const {
  return gps_.location.isValid() && gps_.location.age() < maxAgeMs;
}

float GpsReader::accuracyM() {
  TinyGPSCustom *pairs[][2] = {{&gnLatErr_, &gnLonErr_}, {&gpLatErr_, &gpLonErr_}};

  for (auto &pair : pairs) {
    if (pair[0]->isValid() && pair[1]->isValid() && pair[0]->age() < 3000) {
      float a = atof(pair[0]->value());
      float b = atof(pair[1]->value());
      if (a > 0 && b > 0) return sqrtf(a * a + b * b);
    }
  }

  // No GST: estimate from HDOP. Roughly 3 m per unit of HDOP for modern
  // modules; the server only needs a rough figure.
  if (gps_.hdop.isValid()) {
    return fmaxf(2.0f, static_cast<float>(gps_.hdop.hdop()) * 3.0f);
  }

  return 50.0f;
}
