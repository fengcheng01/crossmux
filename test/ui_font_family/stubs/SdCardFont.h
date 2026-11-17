#pragma once
#include <cstdint>
#include <string>
#include "EpdFontFamily.h"
class SdCardFont {
 public:
  inline static unsigned live = 0;
  std::string path;
  EpdFont font;
  bool psramMetadata = false;
  SdCardFont() { ++live; }
  ~SdCardFont() { --live; }
  bool load(const char* p, bool, bool, uint8_t, bool psram = false) {
    path = p;
    psramMetadata = psram;
    return path != "missing";
  }
  uint32_t contentHash() const { return 123; }
  EpdFont* getEpdFont(uint8_t) { return &font; }
  unsigned styleCount() const { return 1; }
  bool usingFlash() const { return false; }
};
