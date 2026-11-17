#pragma once
#include <map>
#include "EpdFontFamily.h"
class SdCardFont;
class GfxRenderer {
 public:
  std::map<int, EpdFontFamily> fonts;
  std::map<int, SdCardFont*> sd;
  const auto& getFontMap() const { return fonts; }
  void registerSdCardFont(int id, SdCardFont* font) { sd[id] = font; }
  void insertFont(int id, EpdFontFamily font) { fonts.emplace(id, font); }
  void removeFont(int id) { fonts.erase(id); }
  void clearFallbackFonts() {}
  void clearSdCardFonts() { sd.clear(); }
};
