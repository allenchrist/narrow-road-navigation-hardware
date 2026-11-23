#pragma once
struct Preferences {
  bool begin(const char*, bool = false) { return true; }
  void end() {}
  bool getBool(const char*, bool d) { return d; }
  void putBool(const char*, bool) {}
};
