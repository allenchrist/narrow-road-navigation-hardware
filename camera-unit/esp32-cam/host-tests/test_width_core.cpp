// Checks width_core.cpp on a laptop against the Python version, using the
// same synthetic lane pictures. From this folder:
//
//   python3 ../../python/tests/export_cases.py cases
//   g++ -std=c++17 -I../src test_width_core.cpp ../src/width_core.cpp -o t && ./t cases
//
// cases.json is read with a few lines of string searching, to need nothing
// beyond the C++ standard library.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/width_core.h"

static bool readPgm(const std::string &path, std::vector<uint8_t> &pixels, int &w, int &h) {
  std::ifstream f(path, std::ios::binary);
  std::string magic;
  int maxValue;
  if (!(f >> magic >> w >> h >> maxValue) || magic != "P5") return false;
  f.get();
  pixels.resize(static_cast<size_t>(w) * h);
  f.read(reinterpret_cast<char *>(pixels.data()), pixels.size());
  return static_cast<size_t>(f.gcount()) == pixels.size();
}

static std::string slurp(const std::string &path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static double numberAfter(const std::string &s, const std::string &key, size_t from) {
  size_t p = s.find("\"" + key + "\"", from);
  p = s.find(':', p);
  return std::stod(s.substr(p + 1));
}

static bool boolAfter(const std::string &s, const std::string &key, size_t from) {
  size_t p = s.find("\"" + key + "\"", from);
  p = s.find(':', p);
  return s.compare(s.find_first_not_of(" ", p + 1), 4, "true") == 0;
}

int main(int argc, char **argv) {
  std::string folder = argc > 1 ? argv[1] : "cases";
  std::string json = slurp(folder + "/cases.json");
  if (json.empty()) {
    std::printf("No %s/cases.json: run export_cases.py first\n", folder.c_str());
    return 1;
  }

  Homography toGround, toImage;
  size_t p = json.find('[', json.find("imageToGround"));
  for (int i = 0; i < 9; i++) {
    toGround.m[i] = std::stod(json.substr(p + 1));
    p = json.find_first_of(",]", p + 1);
  }
  invertHomography(toGround, toImage);

  int failures = 0;
  size_t at = json.find("\"cases\"");
  while ((at = json.find("\"name\"", at + 1)) != std::string::npos) {
    size_t q1 = json.find('"', json.find(':', at)) + 1;
    std::string name = json.substr(q1, json.find('"', q1) - q1);
    double truth = numberAfter(json, "trueWidth", at);
    size_t py = json.find("\"python\"", at);
    bool pyValid = boolAfter(json, "valid", py), pyWide = boolAfter(json, "wide", py);
    double pyWidth = numberAfter(json, "width", py);

    std::vector<uint8_t> img;
    int w, h;
    if (!readPgm(folder + "/" + name + ".pgm", img, w, h)) {
      std::printf("%s: can't read picture\n", name.c_str());
      failures++;
      continue;
    }

    WidthResult r = measureWidth(img.data(), w, h, toGround, toImage, WidthSettings());
    bool same = r.valid == pyValid && r.wide == pyWide && std::fabs(r.width - pyWidth) < 0.01;
    bool close = r.wide ? truth > 8 : std::fabs(r.width - truth) < 0.15;
    std::printf("%-16s true %5.2f  C++ %s%5.2f  Python %s%5.2f  %s\n", name.c_str(), truth,
                r.wide ? ">" : " ", r.width, pyWide ? ">" : " ", pyWidth,
                same && close ? "ok" : "FAIL");
    if (!same || !close) failures++;
  }

  std::printf(failures ? "%d failed\n" : "All match\n", failures);
  return failures ? 1 : 0;
}
