#pragma once

#if FREEINK_DEVICE_MURPHY_M4

#include <HalStorage.h>
#include <cstdint>
#include <memory>
#include <string>
#include "EpdFont.h"
#include "EpdFontData.h"

// Forward declaration of stbtt_fontinfo
struct stbtt_fontinfo;

class MurphyM4TtfFont {
 public:
  MurphyM4TtfFont();
  ~MurphyM4TtfFont();

  MurphyM4TtfFont(const MurphyM4TtfFont&) = delete;
  MurphyM4TtfFont& operator=(const MurphyM4TtfFont&) = delete;

  // Load a .ttf or .otf file from SD card into PSRAM.
  // Supports fonts of any size (1MB to 50MB+) via dual-mode in-memory / streamed loading.
  bool load(const char* path, uint8_t pointSize);

  // Update point size (resets glyph/advance cache, retains TTF metadata in PSRAM).
  bool setPointSize(uint8_t pointSize);
  uint8_t pointSize() const { return pointSize_; }
  uint32_t contentHash() const { return contentHash_; }

  // Returns pointer to the EpdFont interface
  EpdFont* getEpdFont(uint8_t style = 0);
  EpdFont* getEpdFontForSize(uint8_t pointSize);

  // Advance measurement for layout (always 0 disk I/O, runs from PSRAM hmtx)
  uint16_t getAdvance(uint32_t cp, uint8_t size = 0) const;

  // Clear resident PSRAM glyph & advance caches
  void clearCache();

  // On-demand glyph handler for EpdFontData
  const EpdGlyph* onGlyphMiss(uint32_t cp);
  const EpdGlyph* onGlyphMissForSize(uint32_t cp, uint8_t size, float scale);
  bool hasCodepoint(uint32_t cp) const;
  using TextGetter = const char* (*)(const void* ctx, uint32_t index);
  int prewarm(TextGetter getter, const void* ctx, uint32_t textCount);

 private:
  struct SizeSlot {
    uint8_t pointSize = 0;
    float scale = 0.0f;
    EpdFontData fontData{};
    std::unique_ptr<EpdFont> epdFont;
    MurphyM4TtfFont* parent = nullptr;
  };

  struct AdvanceEntry {
    uint64_t key = 0xFFFFFFFFFFFFFFFFULL;
    uint16_t advanceFP = 0;
  };

  struct GlyphCacheEntry {
    uint64_t key = 0xFFFFFFFFFFFFFFFFULL;
    EpdGlyph glyph{};
    uint32_t bitmapOffset = 0;
  };

  void clearGlyphCache();
  static const EpdGlyph* glyphMissHandlerStatic(void* ctx, uint32_t cp);
  static const EpdGlyph* sizeSlotGlyphMissHandler(void* ctx, uint32_t cp);
  static bool coverageHandlerStatic(void* ctx, uint32_t cp);
  static bool sizeSlotCoverageHandler(void* ctx, uint32_t cp);

  static constexpr size_t ADVANCE_CACHE_SIZE = 4096;
  static constexpr size_t GLYPH_CACHE_SIZE = 8192;
  static constexpr size_t BITMAP_ARENA_SIZE = 2 * 1024 * 1024;  // 2 MB PSRAM bitmap arena

  std::string fontPath_;
  uint8_t* fontFileBuffer_ = nullptr;
  size_t fontFileSize_ = 0;

  bool isStreamed_ = false;
  uint32_t glyfFileOffset_ = 0;
  uint32_t glyfLength_ = 0;
  HalFile fontFile_;
  stbtt_fontinfo* fontInfo_ = nullptr;

  uint8_t pointSize_ = 14;
  float scale_ = 0.0f;
  uint32_t contentHash_ = 0;
  bool isLoaded_ = false;

  AdvanceEntry* advanceCache_ = nullptr;
  GlyphCacheEntry* glyphCache_ = nullptr;
  uint8_t* bitmapArena_ = nullptr;
  size_t bitmapArenaUsed_ = 0;

  uint8_t* scratchBuffer_ = nullptr;
  uint8_t* alphaBuffer_ = nullptr;

  std::vector<std::unique_ptr<SizeSlot>> sizeSlots_;
  EpdFontData fontData_{};
  std::unique_ptr<EpdFont> epdFont_;
};

#endif  // FREEINK_DEVICE_MURPHY_M4
