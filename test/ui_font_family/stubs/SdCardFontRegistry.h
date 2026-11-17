#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct SdCardFontFileInfo { std::string path; uint8_t pointSize; };
struct SdCardFontFamilyInfo {
  std::string name;
  std::vector<SdCardFontFileInfo> files;
  const SdCardFontFileInfo* findFile(uint8_t size) const {
    for (const auto& file : files) if (file.pointSize == size) return &file;
    return nullptr;
  }
  const SdCardFontFileInfo* findNearestSize(uint8_t size) const { return findFile(size); }
};
