#include "MurphyM4TtfFont.h"


#if FREEINK_DEVICE_MURPHY_M4

#include <HalStorage.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#if defined(BOARD_HAS_PSRAM)
#include <esp_heap_caps.h>
#define STBTT_malloc(x, u) ((void)(u), heap_caps_malloc(x, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
#define STBTT_free(x, u) ((void)(u), free(x))
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace {

void* allocPsramOrHeap(size_t size) {
#if defined(BOARD_HAS_PSRAM)
  void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ptr) return ptr;
  LOG_ERR("M4TTF", "PSRAM alloc failed for %zu bytes (free_psram=%u, max_block=%u)", size,
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
#endif
  return malloc(size);
}

void freePsramOrHeap(void* ptr) {
  if (ptr) free(ptr);
}

constexpr uint32_t FNV_OFFSET = 2166136261u;
constexpr uint32_t FNV_PRIME = 16777619u;

uint32_t hashString(const char* str) {
  uint32_t hash = FNV_OFFSET;
  while (*str) {
    hash ^= static_cast<uint8_t>(*str++);
    hash *= FNV_PRIME;
  }
  return hash;
}

inline size_t cacheSlot(uint32_t key, size_t mask) {
  return ((key * 2654435761u) ^ (key >> 16)) & mask;
}

}  // namespace

MurphyM4TtfFont::MurphyM4TtfFont() {
  fontInfo_ = new stbtt_fontinfo();
  memset(fontInfo_, 0, sizeof(stbtt_fontinfo));
}

MurphyM4TtfFont::~MurphyM4TtfFont() {
  fontFile_.close();
  clearCache();

  if (scratchBuffer_) {
    freePsramOrHeap(scratchBuffer_);
    scratchBuffer_ = nullptr;
  }
  if (alphaBuffer_) {
    freePsramOrHeap(alphaBuffer_);
    alphaBuffer_ = nullptr;
  }
  if (advanceCache_) {
    freePsramOrHeap(advanceCache_);
    advanceCache_ = nullptr;
  }
  if (glyphCache_) {
    freePsramOrHeap(glyphCache_);
    glyphCache_ = nullptr;
  }
  if (bitmapArena_) {
    freePsramOrHeap(bitmapArena_);
    bitmapArena_ = nullptr;
  }
  if (fontFileBuffer_) {
    freePsramOrHeap(fontFileBuffer_);
    fontFileBuffer_ = nullptr;
  }

  delete fontInfo_;
  fontInfo_ = nullptr;
}

const EpdGlyph* MurphyM4TtfFont::glyphMissHandlerStatic(void* ctx, uint32_t cp) {
  return static_cast<MurphyM4TtfFont*>(ctx)->onGlyphMiss(cp);
}

const EpdGlyph* MurphyM4TtfFont::sizeSlotGlyphMissHandler(void* ctx, uint32_t cp) {
  auto* slot = static_cast<SizeSlot*>(ctx);
  return slot->parent->onGlyphMissForSize(cp, slot->pointSize, slot->scale);
}

bool MurphyM4TtfFont::coverageHandlerStatic(void* ctx, uint32_t cp) {
  return static_cast<MurphyM4TtfFont*>(ctx)->hasCodepoint(cp);
}

bool MurphyM4TtfFont::sizeSlotCoverageHandler(void* ctx, uint32_t cp) {
  auto* slot = static_cast<SizeSlot*>(ctx);
  return slot->parent->hasCodepoint(cp);
}
bool MurphyM4TtfFont::load(const char* path, uint8_t pointSize) {
  if (!path || !*path) return false;

  LOG_INF("M4TTF", "Loading TTF font: %s (size %u)", path, pointSize);

  HalFile file = Storage.open(path);
  if (!file) {
    LOG_ERR("M4TTF", "Failed to open TTF file: %s", path);
    return false;
  }

  size_t fileSize = file.size();
  if (fileSize == 0) {
    LOG_ERR("M4TTF", "Invalid TTF file size: %zu bytes", fileSize);
    file.close();
    return false;
  }

  fontPath_ = path;
  fontFileSize_ = fileSize;
  contentHash_ = hashString(path);

  if (fontFileBuffer_) {
    freePsramOrHeap(fontFileBuffer_);
    fontFileBuffer_ = nullptr;
  }

  size_t maxBlock = 0;
#if defined(BOARD_HAS_PSRAM)
  maxBlock = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
#endif

  // If the font is <= 5MB and fits comfortably in PSRAM with headroom, load in-memory.
  // Otherwise, use the compact streaming mode (works for 10MB, 20MB, 50MB+ fonts).
  if (fileSize <= 5 * 1024 * 1024 && maxBlock > (fileSize + 1536 * 1024)) {
    isStreamed_ = false;
    LOG_INF("M4TTF", "In-memory mode: %s (%zu KB)", path, fileSize / 1024);
    fontFileBuffer_ = static_cast<uint8_t*>(allocPsramOrHeap(fileSize));
    if (!fontFileBuffer_) {
      file.close();
      return false;
    }

    size_t bytesRead = 0;
    constexpr size_t CHUNK_SIZE = 16 * 1024;
    while (bytesRead < fileSize) {
      size_t toRead = std::min(CHUNK_SIZE, fileSize - bytesRead);
      int n = file.read(fontFileBuffer_ + bytesRead, toRead);
      if (n <= 0) break;
      bytesRead += static_cast<size_t>(n);
    }
    file.close();

    if (bytesRead != fileSize) {
      LOG_ERR("M4TTF", "Read mismatch: read %zu of %zu bytes", bytesRead, fileSize);
      freePsramOrHeap(fontFileBuffer_);
      fontFileBuffer_ = nullptr;
      return false;
    }

    int offset = stbtt_GetFontOffsetForIndex(fontFileBuffer_, 0);
    if (offset < 0) offset = 0;
    if (!stbtt_InitFont(fontInfo_, fontFileBuffer_, offset)) {
      LOG_ERR("M4TTF", "stbtt_InitFont failed on %s (offset=%d)", path, offset);
      freePsramOrHeap(fontFileBuffer_);
      fontFileBuffer_ = nullptr;
      return false;
    }
  } else {
    // STREAMING MODE:
    isStreamed_ = true;
    uint8_t hdr[12];
    if (file.read(hdr, 12) != 12) {
      LOG_ERR("M4TTF", "Failed to read font header: %s", path);
      file.close();
      return false;
    }
    uint16_t numTables = (hdr[4] << 8) | hdr[5];
    if (numTables == 0 || numTables > 64) {
      LOG_ERR("M4TTF", "Invalid numTables (%u) in %s", numTables, path);
      file.close();
      return false;
    }

    struct TableEntry {
      char tag[5];
      uint32_t checkSum;
      uint32_t offset;
      uint32_t length;
    };
    std::vector<TableEntry> tables(numTables);
    for (uint16_t i = 0; i < numTables; ++i) {
      uint8_t rec[16];
      if (file.read(rec, 16) != 16) {
        file.close();
        return false;
      }
      memcpy(tables[i].tag, rec, 4);
      tables[i].tag[4] = '\0';
      tables[i].checkSum = (rec[4] << 24) | (rec[5] << 16) | (rec[6] << 8) | rec[7];
      tables[i].offset = (rec[8] << 24) | (rec[9] << 16) | (rec[10] << 8) | rec[11];
      tables[i].length = (rec[12] << 24) | (rec[13] << 16) | (rec[14] << 8) | rec[15];
    }

    glyfFileOffset_ = 0;
    glyfLength_ = 0;
    for (const auto& t : tables) {
      if (strcmp(t.tag, "glyf") == 0) {
        glyfFileOffset_ = t.offset;
        glyfLength_ = t.length;
        break;
      }
    }

    size_t tableDirSize = 12 + numTables * 16;
    size_t totalMetadataSize = tableDirSize;
    for (const auto& t : tables) {
      if (strcmp(t.tag, "glyf") != 0) {
        totalMetadataSize += (t.length + 3) & ~3;
      }
    }

    LOG_INF("M4TTF", "Streaming mode: font size=%zu KB, compact metadata=%zu KB, glyf offset=%u (%zu KB)",
            fileSize / 1024, totalMetadataSize / 1024, glyfFileOffset_, glyfLength_ / 1024);

    fontFileBuffer_ = static_cast<uint8_t*>(allocPsramOrHeap(totalMetadataSize));
    if (!fontFileBuffer_) {
      LOG_ERR("M4TTF", "Failed to allocate %zu bytes for compact metadata", totalMetadataSize);
      file.close();
      return false;
    }

    memcpy(fontFileBuffer_, hdr, 12);
    size_t currentOffset = tableDirSize;
    for (uint16_t i = 0; i < numTables; ++i) {
      uint8_t* rec = fontFileBuffer_ + 12 + i * 16;
      memcpy(rec, tables[i].tag, 4);
      uint32_t recCheckSum = tables[i].checkSum;
      rec[4] = (recCheckSum >> 24) & 0xFF;
      rec[5] = (recCheckSum >> 16) & 0xFF;
      rec[6] = (recCheckSum >> 8) & 0xFF;
      rec[7] = recCheckSum & 0xFF;

      if (strcmp(tables[i].tag, "glyf") == 0) {
        uint32_t off = glyfFileOffset_;
        rec[8] = (off >> 24) & 0xFF;
        rec[9] = (off >> 16) & 0xFF;
        rec[10] = (off >> 8) & 0xFF;
        rec[11] = off & 0xFF;
        uint32_t len = tables[i].length;
        rec[12] = (len >> 24) & 0xFF;
        rec[13] = (len >> 16) & 0xFF;
        rec[14] = (len >> 8) & 0xFF;
        rec[15] = len & 0xFF;
      } else {
        file.seekSet(tables[i].offset);
        file.read(fontFileBuffer_ + currentOffset, tables[i].length);

        uint32_t off = static_cast<uint32_t>(currentOffset);
        rec[8] = (off >> 24) & 0xFF;
        rec[9] = (off >> 16) & 0xFF;
        rec[10] = (off >> 8) & 0xFF;
        rec[11] = off & 0xFF;
        uint32_t len = tables[i].length;
        rec[12] = (len >> 24) & 0xFF;
        rec[13] = (len >> 16) & 0xFF;
        rec[14] = (len >> 8) & 0xFF;
        rec[15] = len & 0xFF;

        currentOffset += (tables[i].length + 3) & ~3;
      }
    }
    // Keep fontFile_ open across reading so on-demand glyph reads avoid FAT lookup latency!
    fontFile_ = std::move(file);

    if (!stbtt_InitFont(fontInfo_, fontFileBuffer_, 0)) {
      LOG_ERR("M4TTF", "stbtt_InitFont failed on streaming font: %s", path);
      freePsramOrHeap(fontFileBuffer_);
      fontFileBuffer_ = nullptr;
      return false;
    }
  }

  isLoaded_ = true;

  // Allocate caches in PSRAM if not already allocated
  if (!advanceCache_) {
    advanceCache_ = static_cast<AdvanceEntry*>(allocPsramOrHeap(ADVANCE_CACHE_SIZE * sizeof(AdvanceEntry)));
  }
  if (!glyphCache_) {
    glyphCache_ = static_cast<GlyphCacheEntry*>(allocPsramOrHeap(GLYPH_CACHE_SIZE * sizeof(GlyphCacheEntry)));
  }
  if (!bitmapArena_) {
    bitmapArena_ = static_cast<uint8_t*>(allocPsramOrHeap(BITMAP_ARENA_SIZE));
  }

  setPointSize(pointSize > 0 ? pointSize : 14);

  LOG_INF("M4TTF", "TTF initialized successfully: %s (%zu KB, size=%upt)", path, fileSize / 1024, pointSize_);
  return true;
}

bool MurphyM4TtfFont::setPointSize(uint8_t pointSize) {
  if (!isLoaded_) return false;
  if (pointSize_ == pointSize && scale_ > 0.0f) return true;

  pointSize_ = pointSize;
  float emPixels = pointSize_ * (150.0f / 72.0f);
  scale_ = stbtt_ScaleForMappingEmToPixels(fontInfo_, emPixels);
  if (scale_ <= 0.0f) {
    scale_ = stbtt_ScaleForPixelHeight(fontInfo_, emPixels);
  }

  // Clear size slots so reader size slot reconfigures
  sizeSlots_.clear();
  clearCache();
  return true;
}

EpdFont* MurphyM4TtfFont::getEpdFontForSize(uint8_t pointSize) {
  if (!isLoaded_) return nullptr;
  if (pointSize == 0) pointSize = pointSize_;

  for (const auto& slot : sizeSlots_) {
    if (slot->pointSize == pointSize) return slot->epdFont.get();
  }

  auto slot = std::make_unique<SizeSlot>();
  slot->pointSize = pointSize;
  slot->parent = this;
  float emPixels = pointSize * (150.0f / 72.0f);
  slot->scale = stbtt_ScaleForMappingEmToPixels(fontInfo_, emPixels);
  if (slot->scale <= 0.0f) {
    slot->scale = stbtt_ScaleForPixelHeight(fontInfo_, emPixels);
  }

  memset(&slot->fontData, 0, sizeof(slot->fontData));
  slot->fontData.bitmap = bitmapArena_;
  slot->fontData.is2Bit = true;
  slot->fontData.glyphMissHandler = sizeSlotGlyphMissHandler;
  slot->fontData.glyphMissCtx = slot.get();
  slot->fontData.coverageHandler = sizeSlotCoverageHandler;

  int ascent = 0, descent = 0, lineGap = 0;
  stbtt_GetFontVMetrics(fontInfo_, &ascent, &descent, &lineGap);
  slot->fontData.advanceY = static_cast<uint8_t>(std::max(1.0f, std::round((ascent - descent + lineGap) * slot->scale)));
  slot->fontData.ascender = static_cast<int>(std::round(ascent * slot->scale));
  slot->fontData.descender = static_cast<int>(std::round(descent * slot->scale));

  slot->epdFont = std::make_unique<EpdFont>(&slot->fontData);
  EpdFont* result = slot->epdFont.get();
  sizeSlots_.push_back(std::move(slot));

  LOG_DBG("M4TTF", "Created size-matched EpdFont for %u pt", pointSize);
  return result;
}

EpdFont* MurphyM4TtfFont::getEpdFont(uint8_t) {
  return getEpdFontForSize(pointSize_);
}

int MurphyM4TtfFont::prewarm(TextGetter getter, const void* ctx, uint32_t textCount) {
  if (!isLoaded_ || !getter) return 0;
  for (uint32_t i = 0; i < textCount; ++i) {
    const char* text = getter(ctx, i);
    if (!text) continue;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(text);
    while (*p) {
      uint32_t cp = utf8NextCodepoint(&p);
      if (cp == 0) break;
      onGlyphMiss(cp);
    }
  }
  return 0;
}

void MurphyM4TtfFont::clearCache() {
  if (advanceCache_) {
    for (size_t i = 0; i < ADVANCE_CACHE_SIZE; ++i) {
      advanceCache_[i].key = 0xFFFFFFFFFFFFFFFFULL;
      advanceCache_[i].advanceFP = 0;
    }
  }
  clearGlyphCache();
}

void MurphyM4TtfFont::clearGlyphCache() {
  if (glyphCache_) {
    for (size_t i = 0; i < GLYPH_CACHE_SIZE; ++i) {
      glyphCache_[i].key = 0xFFFFFFFFFFFFFFFFULL;
    }
  }
  bitmapArenaUsed_ = 0;
}

uint16_t MurphyM4TtfFont::getAdvance(uint32_t cp, uint8_t size) const {
  if (!isLoaded_ || !advanceCache_) return 0;
  if (size == 0) size = pointSize_;

  uint64_t key = (static_cast<uint64_t>(cp) << 8) | size;
  size_t slot = cacheSlot(static_cast<uint32_t>(key ^ (key >> 32)), ADVANCE_CACHE_SIZE - 1);
  if (advanceCache_[slot].key == key) {
    return advanceCache_[slot].advanceFP;
  }

  int glyphIndex = stbtt_FindGlyphIndex(fontInfo_, static_cast<int>(cp));
  if (glyphIndex == 0) return 0;

  float scale = scale_;
  if (size != pointSize_) {
    float emPixels = size * (150.0f / 72.0f);
    scale = stbtt_ScaleForMappingEmToPixels(fontInfo_, emPixels);
    if (scale <= 0.0f) scale = stbtt_ScaleForPixelHeight(fontInfo_, emPixels);
  }

  int advanceWidth = 0, lsb = 0;
  stbtt_GetGlyphHMetrics(fontInfo_, glyphIndex, &advanceWidth, &lsb);
  uint16_t advFP = static_cast<uint16_t>(std::round((advanceWidth * scale) * 16.0f));

  advanceCache_[slot].key = key;
  advanceCache_[slot].advanceFP = advFP;
  return advFP;
}

bool MurphyM4TtfFont::hasCodepoint(uint32_t cp) const {
  if (!isLoaded_) return false;
  return stbtt_FindGlyphIndex(fontInfo_, static_cast<int>(cp)) != 0;
}

const EpdGlyph* MurphyM4TtfFont::onGlyphMiss(uint32_t cp) {
  return onGlyphMissForSize(cp, pointSize_, scale_);
}

const EpdGlyph* MurphyM4TtfFont::onGlyphMissForSize(uint32_t cp, uint8_t size, float scale) {
  if (!isLoaded_ || !glyphCache_ || !bitmapArena_) return nullptr;

  uint64_t key = (static_cast<uint64_t>(cp) << 8) | size;
  size_t slot = cacheSlot(static_cast<uint32_t>(key ^ (key >> 32)), GLYPH_CACHE_SIZE - 1);
  if (glyphCache_[slot].key == key) {
    return &glyphCache_[slot].glyph;
  }

  int glyphIndex = stbtt_FindGlyphIndex(fontInfo_, static_cast<int>(cp));
  if (glyphIndex == 0) {
    return nullptr;
  }

  int advanceWidth = 0, lsb = 0;
  stbtt_GetGlyphHMetrics(fontInfo_, glyphIndex, &advanceWidth, &lsb);

  int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
  stbtt_vertex* vertices = nullptr;
  int num_verts = 0;

  if (!isStreamed_) {
    stbtt_GetGlyphBitmapBox(fontInfo_, glyphIndex, scale, scale, &ix0, &iy0, &ix1, &iy1);
    num_verts = stbtt_GetGlyphShape(fontInfo_, glyphIndex, &vertices);
  } else {
    // STREAMING GLYPH LOOKUP FROM SD
    uint32_t g1 = 0, g2 = 0;
    if (fontInfo_->indexToLocFormat == 0) {
      uint16_t off1 = (fontFileBuffer_[fontInfo_->loca + glyphIndex * 2] << 8) |
                      fontFileBuffer_[fontInfo_->loca + glyphIndex * 2 + 1];
      uint16_t off2 = (fontFileBuffer_[fontInfo_->loca + glyphIndex * 2 + 2] << 8) |
                      fontFileBuffer_[fontInfo_->loca + glyphIndex * 2 + 3];
      g1 = off1 * 2;
      g2 = off2 * 2;
    } else {
      const uint8_t* p1 = fontFileBuffer_ + fontInfo_->loca + glyphIndex * 4;
      const uint8_t* p2 = fontFileBuffer_ + fontInfo_->loca + glyphIndex * 4 + 4;
      g1 = (p1[0] << 24) | (p1[1] << 16) | (p1[2] << 8) | p1[3];
      g2 = (p2[0] << 24) | (p2[1] << 16) | (p2[2] << 8) | p2[3];
    }

    if (g1 == g2) {
      // Empty glyph (e.g. whitespace)
      GlyphCacheEntry& entry = glyphCache_[slot];
      entry.key = key;
      entry.bitmapOffset = 0;
      entry.glyph = EpdGlyph{};
      entry.glyph.advanceX = static_cast<uint16_t>(std::round((advanceWidth * scale) * 16.0f));
      return &entry.glyph;
    }

    if (g2 > g1) {
      uint32_t glyphLen = g2 - g1;
      if (!fontFile_) {
        fontFile_ = Storage.open(fontPath_.c_str());
      }
      if (fontFile_ && fontFile_.seekSet(glyfFileOffset_ + g1)) {
        if (!scratchBuffer_) {
          scratchBuffer_ = static_cast<uint8_t*>(allocPsramOrHeap(4096));
        }
        uint8_t* sBuf = scratchBuffer_;
        std::vector<uint8_t> fallbackBuf;
        if (glyphLen > 4096) {
          fallbackBuf.resize(glyphLen);
          sBuf = fallbackBuf.data();
        }
        int nRead = fontFile_.read(sBuf, glyphLen);
        if (nRead > 0) {
          int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
          stbtt_GetGlyphBoxFromData(sBuf, &x0, &y0, &x1, &y1);
          ix0 = static_cast<int>(std::floor(x0 * scale));
          iy0 = static_cast<int>(std::floor(-y1 * scale));
          ix1 = static_cast<int>(std::ceil(x1 * scale));
          iy1 = static_cast<int>(std::ceil(-y0 * scale));

          num_verts = stbtt_GetGlyphShapeFromData(fontInfo_, sBuf, &vertices);
        }
      }
    }
  }

  int w = std::max(0, ix1 - ix0);
  int h = std::max(0, iy1 - iy0);

  size_t totalPixels = static_cast<size_t>(w * h);
  size_t bitmapBytes = (totalPixels + 3) / 4;

  if (bitmapArenaUsed_ + bitmapBytes > BITMAP_ARENA_SIZE) {
    clearGlyphCache();
    slot = cacheSlot(static_cast<uint32_t>(key ^ (key >> 32)), GLYPH_CACHE_SIZE - 1);
  }

  uint32_t offset = static_cast<uint32_t>(bitmapArenaUsed_);
  bitmapArenaUsed_ += bitmapBytes;

  if (bitmapBytes > 0 && w > 0 && h > 0 && num_verts > 0) {
    if (!alphaBuffer_) {
      alphaBuffer_ = static_cast<uint8_t*>(allocPsramOrHeap(16384));
    }
    uint8_t* aBuf = alphaBuffer_;
    std::vector<uint8_t> fallbackAlpha;
    if (totalPixels > 16384) {
      fallbackAlpha.resize(totalPixels);
      aBuf = fallbackAlpha.data();
    }
    memset(aBuf, 0, totalPixels);

    stbtt__bitmap gbm;
    gbm.w = w;
    gbm.h = h;
    gbm.stride = w;
    gbm.pixels = aBuf;
    stbtt_Rasterize(&gbm, 0.35f, vertices, num_verts, scale, scale, 0.0f, 0.0f, ix0, iy0, 1, fontInfo_->userdata);

    uint8_t* dst = &bitmapArena_[offset];
    memset(dst, 0, bitmapBytes);
    for (size_t p = 0; p < totalPixels; ++p) {
      uint8_t a = aBuf[p];
      uint8_t gray = a >> 6;
      dst[p >> 2] |= static_cast<uint8_t>(gray << ((3 - (p & 3)) * 2));
    }
  }

  if (vertices) STBTT_free(vertices, fontInfo_->userdata);

  GlyphCacheEntry& entry = glyphCache_[slot];
  entry.key = key;
  entry.bitmapOffset = offset;
  entry.glyph.width = static_cast<uint8_t>(w);
  entry.glyph.height = static_cast<uint8_t>(h);
  entry.glyph.left = static_cast<int16_t>(ix0);
  entry.glyph.top = static_cast<int16_t>(-iy0);
  entry.glyph.advanceX = static_cast<uint16_t>(std::round((advanceWidth * scale) * 16.0f));
  entry.glyph.dataLength = static_cast<uint16_t>(bitmapBytes);
  entry.glyph.dataOffset = offset;

  return &entry.glyph;
}

#endif  // FREEINK_DEVICE_MURPHY_M4
