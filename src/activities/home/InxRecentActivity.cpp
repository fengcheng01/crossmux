#include "InxRecentActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "CrossPointSettings.h"
#include "InxItemLayout.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "components/themes/inx/InxInkCards.h"
#include "components/themes/inx/InxTheme.h"
#include "fontIds.h"
#include "util/BookCoverLoader.h"
#include "util/PaginationDots.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace {
constexpr int kGap = 8;
constexpr int kPagePadding = 18;
constexpr int kProgressHeight = 6;
constexpr int kHomeBatteryWidth = 15;
constexpr int kHomeBatteryHeight = 12;
constexpr int kHomeBatteryRightMargin = 12;

Rect contentRect(const GfxRenderer& renderer) {
  const Rect content = UITheme::getInstance().getMainTabContentRect(renderer);
  const auto& metrics = UITheme::getInstance().getMetrics();
  return Rect{content.x, content.y, content.width, std::max(0, content.height - metrics.verticalSpacing)};
}

const char* titleOf(const RecentBook& book) { return book.title.empty() ? book.path.c_str() : book.title.c_str(); }

void drawMiniProgress(const GfxRenderer& renderer, const Rect rect, const uint8_t percent) {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  const int innerWidth = std::max(0, rect.width - 2);
  const int fillWidth = innerWidth * std::min<int>(percent, 100) / 100;
  if (fillWidth > 0) renderer.fillRect(rect.x + 1, rect.y + 1, fillWidth, std::max(0, rect.height - 2));
}

uint8_t progressOf(const ReadingBookStats* stats) { return stats ? stats->lastProgressPercent : 0; }

void drawSparseInk(const GfxRenderer& renderer, const Rect rect) {
  for (int y = rect.y; y < rect.y + rect.height; y += 2) {
    for (int x = rect.x; x < rect.x + rect.width; x += 2) renderer.drawPixel(x, y, true);
  }
}

void drawDottedSeparator(const GfxRenderer& renderer, const int x, const int y, const int width) {
  for (int px = x; px < x + width; px += 3) renderer.drawPixel(px, y, true);
}

Rect fitCoverRect(const Rect bounds) {
  const auto size = InxCoverGeometry::fit(bounds.width, bounds.height);
  return Rect{bounds.x + (bounds.width - size.width) / 2, bounds.y + (bounds.height - size.height) / 2, size.width,
              size.height};
}

void drawThickFrame(const GfxRenderer& renderer, const Rect rect, const int thickness = 3) {
  for (int inset = 0; inset < thickness; ++inset) {
    renderer.drawRect(rect.x - inset, rect.y - inset, rect.width + inset * 2, rect.height + inset * 2, true);
  }
}

void drawProgressBadge(const GfxRenderer& renderer, const Rect cover, const uint8_t progress) {
  char label[8];
  snprintf(label, sizeof(label), "%u%%", static_cast<unsigned>(progress));
  const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
  const int textHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int width = std::max(24, textWidth + 8);
  const int height = textHeight + 4;
  const int x = cover.x + cover.width - width - 2;
  const int y = cover.y + 2;
  renderer.fillRect(x, y, width, height, true);
  renderer.drawText(SMALL_FONT_ID, x + (width - textWidth) / 2, y + 2, label, false, EpdFontFamily::BOLD);
}

void drawMetric(const GfxRenderer& renderer, const int x, const int y, const char* value, const char* label,
                const int width) {
  const std::string shownValue = renderer.truncatedText(UI_12_FONT_ID, value, width, EpdFontFamily::BOLD);
  const std::string shownLabel = renderer.truncatedText(SMALL_FONT_ID, label, width);
  renderer.drawText(UI_12_FONT_ID, x, y, shownValue.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, x, y + renderer.getLineHeight(UI_12_FONT_ID) + 4, shownLabel.c_str());
}

int titleFontId() {
  // Card titles stay at 12pt: the reader size can be 14/16/18, which only fits
  // two or three CJK glyphs in the hero column and then ellipsizes. UI_12 is
  // the full common-character subset on CN builds. See BookTitleFont.h.
  return UI_12_FONT_ID;
}

int drawBookText(const GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                 const bool author) {
  const int titleFont = titleFontId();
  const auto titleLines = renderer.wrappedText(titleFont, titleOf(book), width, 3, EpdFontFamily::BOLD);
  int cursorY = y;
  const int titleHeight = renderer.getLineHeight(titleFont);
  for (const auto& line : titleLines) {
    renderer.drawText(titleFont, x, cursorY, line.c_str(), true, EpdFontFamily::BOLD);
    cursorY += titleHeight;
  }
  if (author && !book.author.empty()) {
    cursorY += 4;
    const std::string subtitle = renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), width);
    renderer.drawText(SMALL_FONT_ID, x, cursorY, subtitle.c_str());
    cursorY += renderer.getLineHeight(SMALL_FONT_ID);
  }
  return cursorY;
}
}  // namespace

void InxRecentActivity::selectMainTabContentEdge(const MainTabContentEdge edge) {
  selected = MainTabs::contentEdgeIndex(edge, books ? static_cast<int>(books->size()) : 0);
}

InxRecentLayout InxRecentActivity::layout() const {
  const auto value = static_cast<InxRecentLayout>(SETTINGS.inxRecentLayout);
  return value < InxRecentLayout::Count ? value : InxRecentLayout::Flow;
}

const ReadingBookStats* InxRecentActivity::statsAt(const int index) const {
  return index >= 0 && index < static_cast<int>(bookStats.size()) ? bookStats[index] : nullptr;
}

void InxRecentActivity::onEnter() {
  Activity::onEnter();
  // Enter on the default FAST exactly like the other tabs. The M4's HALF
  // sequence (0xD4) was tried here twice: it black-flashes hard and leaves
  // the whole page (tab bar included) washed out — worse than any FAST
  // artifact. Do not reintroduce it without re-validating the sequence.
  sdFontSystem.ensureLoaded(renderer);
  if (RECENT_BOOKS.pruneMissing()) RECENT_BOOKS.saveToFile();
  books = &RECENT_BOOKS.getBooks();
  bookStats.fill(nullptr);
  for (size_t i = 0; i < std::min(books->size(), bookStats.size()); ++i) {
    const RecentBook& book = (*books)[i];
    bookStats[i] = READING_STATS.findMatchingBookForPath(book.path, book.title, book.author);
  }
  selected = 0;
  thumbnailHeight = 0;
  targetCoverStates.fill(CoverCacheState::Unchecked);
  fallbackCoverStates.fill(CoverCacheState::Unchecked);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  clearCoverCaches();
#endif
  requestUpdate();
}

void InxRecentActivity::onExit() {
  books = nullptr;
  bookStats.fill(nullptr);
  targetCoverStates.fill(CoverCacheState::Unchecked);
  fallbackCoverStates.fill(CoverCacheState::Unchecked);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  clearCoverCaches();
#endif
  thumbnailHeight = 0;
  Activity::onExit();
}

void InxRecentActivity::openSelected() {
  if (!books || selected < 0 || selected >= static_cast<int>(books->size())) return;
  onSelectBook((*books)[selected].path);
}

void InxRecentActivity::setThumbnailHeight(const int height) {
  if (thumbnailHeight == height) return;
  thumbnailHeight = height;
  targetCoverStates.fill(CoverCacheState::Unchecked);
  fallbackCoverStates.fill(CoverCacheState::Unchecked);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  clearCoverCaches();
#endif
}

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
void InxRecentActivity::clearCoverCaches() {
  std::for_each(targetCoverCaches.begin(), targetCoverCaches.end(), [](auto& cache) { cache = {}; });
  std::for_each(fallbackCoverCaches.begin(), fallbackCoverCaches.end(), [](auto& cache) { cache = {}; });
  cachedCoverBytes = 0;
}

InxRecentActivity::CoverCacheLoadResult InxRecentActivity::tryLoadCoverCache(HalFile& file, CoverRamCache& cache) {
  if (cache.attempted) return CoverCacheLoadResult::Stream;
  cache.attempted = true;

  const size_t fileSize = file.fileSize();
  if (fileSize == 0 || fileSize > MAX_CACHED_COVER_FILE_BYTES || cachedCoverBytes > MAX_COVER_CACHE_BYTES ||
      fileSize > MAX_COVER_CACHE_BYTES - cachedCoverBytes) {
    return CoverCacheLoadResult::Stream;
  }
  if (!memory::psramHasHeadroom(fileSize, fileSize, COVER_CACHE_MAX_ALLOC_RESERVE)) {
    return CoverCacheLoadResult::Stream;
  }

  // A cover is up to 64 KB, so it cannot live on the task stack. Allocate once
  // on first use and retain it for this Activity.
  auto bytes = memory::makePsramByteBufferNoThrow(fileSize);
  if (!bytes) return CoverCacheLoadResult::Stream;
  if (!file.seek(0) || file.read(bytes.get(), fileSize) != static_cast<int>(fileSize)) {
    LOG_ERR("INX", "Short read loading cover into PSRAM (%u bytes)", static_cast<unsigned>(fileSize));
    file.seek(0);
    return CoverCacheLoadResult::Stream;
  }

  Bitmap bitmap(bytes.get(), fileSize);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    return CoverCacheLoadResult::Invalid;
  }

  cache.bytes = std::move(bytes);
  cache.size = fileSize;
  cachedCoverBytes += fileSize;
  return CoverCacheLoadResult::Loaded;
}
#endif

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
bool InxRecentActivity::tryDrawBookCover(const std::string& path, const Rect& bounds, CoverCacheState& state,
                                         CoverRamCache& cache) {
#else
bool InxRecentActivity::tryDrawBookCover(const std::string& path, const Rect& bounds, CoverCacheState& state) {
#endif
  switch (state) {
    case CoverCacheState::Unchecked:
      if (!Storage.exists(path.c_str())) {
        state = CoverCacheState::Missing;
        return false;
      }
      state = CoverCacheState::Ready;
      break;
    case CoverCacheState::Ready:
      break;
    case CoverCacheState::Missing:
    case CoverCacheState::Unavailable:
      return false;
  }

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  const auto drawCached = [this, &bounds, &state, &cache] {
    Bitmap bitmap(cache.bytes.get(), cache.size);
    if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
      cachedCoverBytes -= cache.size;
      cache = {};
      state = CoverCacheState::Missing;
      return false;
    }
    return renderer.drawBitmapCropToFill(bitmap, bounds.x, bounds.y, bounds.width, bounds.height);
  };

  if (cache.bytes) return drawCached();
#endif

  HalFile file;
  if (!Storage.openFileForRead("INX", path, file)) {
    state = CoverCacheState::Missing;
    return false;
  }

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  switch (tryLoadCoverCache(file, cache)) {
    case CoverCacheLoadResult::Loaded:
      return drawCached();
    case CoverCacheLoadResult::Invalid:
      state = CoverCacheState::Missing;
      return false;
    case CoverCacheLoadResult::Stream:
      break;
  }
#endif

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    state = CoverCacheState::Missing;
    return false;
  }
  return renderer.drawBitmapCropToFill(bitmap, bounds.x, bounds.y, bounds.width, bounds.height);
}

bool InxRecentActivity::drawBookCover(const int bookIndex, const Rect& bounds) {
  renderer.fillRect(bounds.x, bounds.y, bounds.width, bounds.height, false);
  if (books && bookIndex >= 0 && bookIndex < static_cast<int>(books->size()) &&
      bookIndex < static_cast<int>(RecentBooksStore::MAX_RECENT_BOOKS) && thumbnailHeight > 0) {
    const RecentBook& book = (*books)[bookIndex];
    if (!book.coverBmpPath.empty()) {
      std::string path = UITheme::getCoverThumbPath(book.coverBmpPath, thumbnailHeight);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
      if (tryDrawBookCover(path, bounds, targetCoverStates[bookIndex], targetCoverCaches[bookIndex])) return true;
#else
      if (tryDrawBookCover(path, bounds, targetCoverStates[bookIndex])) return true;
#endif
      if (book.coverBmpPath.find("[HEIGHT]") != std::string::npos &&
          thumbnailHeight != InxMetrics::values.homeCoverHeight) {
        path = UITheme::getCoverThumbPath(book.coverBmpPath, InxMetrics::values.homeCoverHeight);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
        if (tryDrawBookCover(path, bounds, fallbackCoverStates[bookIndex], fallbackCoverCaches[bookIndex])) return true;
#else
        if (tryDrawBookCover(path, bounds, fallbackCoverStates[bookIndex])) return true;
#endif
      }
    }
  }

  const auto size = InxCoverGeometry::fit(bounds.width, bounds.height);
  const int x = bounds.x + (bounds.width - size.width) / 2;
  const int y = bounds.y + (bounds.height - size.height) / 2;
  renderer.drawRect(x, y, size.width, size.height, 2, true);
  renderer.fillRect(x, y + size.height / 3, size.width, size.height * 2 / 3);
  constexpr int iconSize = 32;
  renderer.drawIcon(CoverIcon, x + (size.width - iconSize) / 2, y + size.height / 6 - iconSize / 2, iconSize);
  return false;
}

bool InxRecentActivity::prepareNextMissingCover() {
  if (!books || thumbnailHeight <= 0) return false;
  const int bookCount = static_cast<int>(std::min(books->size(), RecentBooksStore::MAX_RECENT_BOOKS));
  const int start = InxRecentGeometry::pageStart(selected, bookCount, layout());
  const int visible = std::min(InxRecentGeometry::itemsPerPage(layout()), std::max(0, bookCount - start));
  bool needsGenerate = false;
  for (int slot = 0; slot < visible; ++slot) {
    const int index = start + slot;
    CoverCacheState& state = targetCoverStates[index];
    switch (state) {
      case CoverCacheState::Unchecked: {
        const std::string& templatePath = (*books)[index].coverBmpPath;
        if (templatePath.empty()) {
          state = CoverCacheState::Unavailable;
          break;
        }
        state = Storage.exists(UITheme::getCoverThumbPath(templatePath, thumbnailHeight).c_str())
                    ? CoverCacheState::Ready
                    : CoverCacheState::Missing;
        break;
      }
      case CoverCacheState::Ready:
      case CoverCacheState::Unavailable:
      case CoverCacheState::Missing:
        break;
    }
    if (state == CoverCacheState::Missing) needsGenerate = true;
  }
  if (!needsGenerate) return false;

  // Commit the placeholder list once, then build every visible thumb before the
  // next paint. One-cover-per-refresh was flashing the whole page N times.
  renderer.displayBuffer();
  {
    GfxRenderer::FrameBufferLoan loan(renderer);
    for (int slot = 0; slot < visible; ++slot) {
      const int index = start + slot;
      if (targetCoverStates[index] != CoverCacheState::Missing) continue;
      targetCoverStates[index] = BookCoverLoader::ensureThumbnail((*books)[index].path, thumbnailHeight).empty()
                                     ? CoverCacheState::Unavailable
                                     : CoverCacheState::Ready;
    }
  }
  requestUpdate();
  return true;
}

int InxRecentActivity::indexFromPoint(const int x, const int y) const {
  if (!books || books->empty()) return -1;
  const Rect content = contentRect(renderer);
  if (x < content.x || x >= content.x + content.width || y < content.y || y >= content.y + content.height) return -1;

  const InxRecentLayout currentLayout = layout();
  const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), currentLayout);
  int columns = 1;
  int rows = 1;
  switch (currentLayout) {
    case InxRecentLayout::Grid:
      columns = 2;
      rows = 2;
      break;
    case InxRecentLayout::List:
      rows = 5;
      break;
    case InxRecentLayout::Icons:
      columns = 3;
      rows = 3;
      break;
    case InxRecentLayout::Flow: {
      if (flowRowStep_ <= 0 || flowVisible_ <= 0) return -1;
      if (y < flowListTop_ || y >= flowListTop_ + flowRowStep_ * flowVisible_) return -1;
      const int slot = (y - flowListTop_) / flowRowStep_;
      const int index = start + slot;
      return index < static_cast<int>(books->size()) ? index : -1;
    }
    case InxRecentLayout::Cover:
      return selected;
    case InxRecentLayout::Count:
      return -1;
  }

  const int column = std::min(columns - 1, (x - content.x) * columns / std::max(1, content.width));
  const int row = std::min(rows - 1, (y - content.y) * rows / std::max(1, content.height));
  const int index = start + row * columns + column;
  return index < static_cast<int>(books->size()) ? index : -1;
}

void InxRecentActivity::loop() {
  if (!books || books->empty()) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
    return;
  }

  const int count = static_cast<int>(books->size());
  const bool showSelection = showMainTabContentSelection();
  const auto currentLayout = layout();
  const auto swipe = mappedInput.wasSwipe();
  const auto stepPage = [this, count, currentLayout](const int delta) {
    const int pageItems = InxRecentGeometry::itemsPerPage(currentLayout);
    const int start = InxRecentGeometry::pageStart(selected, count, currentLayout);
    if (delta > 0) {
      const int next = start + pageItems;
      selected = next < count ? next : start;
    } else {
      selected = start <= 0 ? 0 : start - pageItems;
    }
  };
  if (mappedInput.wasReleased(MappedInputManager::Button::NavNext) || swipe == MappedInputManager::SwipeDir::Up ||
      swipe == MappedInputManager::SwipeDir::Left) {
    if (count > 1) {
      const int previousStart = InxRecentGeometry::pageStart(selected, count, currentLayout);
      if (showSelection) {
        selected = (selected + 1) % count;
      } else {
        stepPage(1);
      }
      if (showSelection || InxRecentGeometry::pageStart(selected, count, currentLayout) != previousStart) {
        requestUpdate();
      }
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious) || swipe == MappedInputManager::SwipeDir::Down ||
      swipe == MappedInputManager::SwipeDir::Right) {
    if (count > 1) {
      const int previousStart = InxRecentGeometry::pageStart(selected, count, currentLayout);
      if (showSelection) {
        selected = (selected + count - 1) % count;
      } else {
        stepPage(-1);
      }
      if (showSelection || InxRecentGeometry::pageStart(selected, count, currentLayout) != previousStart) {
        requestUpdate();
      }
    }
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    if (currentLayout == InxRecentLayout::Flow && x >= moreHitRect_.x && y >= moreHitRect_.y &&
        x < moreHitRect_.x + moreHitRect_.width && y < moreHitRect_.y + moreHitRect_.height) {
      activityManager.goToMainTab(MainTab::Statistics);
      return;
    }
    const int touched = indexFromPoint(x, y);
    if (touched >= 0) {
      selected = touched;
      openSelected();
    }
    return;
  }
  if (mappedInput.wasScreenTouchDown(x, y)) {
    if (!showSelection) return;
    const int touched = indexFromPoint(x, y);
    if (touched >= 0 && touched != selected) {
      selected = touched;
      requestUpdate();
    }
  }
}

void InxRecentActivity::drawFlow(const Rect& content) {
  const int bookCount = books ? static_cast<int>(books->size()) : 0;
  const int pad = 10;
  const int titleFont = titleFontId();
  const int titleLineH = renderer.getLineHeight(titleFont);
  const int chapterFont = UI_10_FONT_ID;
  const int chapterH = renderer.getLineHeight(chapterFont);
  const int headH = renderer.getLineHeight(UI_12_FONT_ID) + chapterH + 4;

  char timeBuf[16] = {};
  TimeUtils::formatCurrentTime(timeBuf, sizeof(timeBuf), SETTINGS.clockFormat == 1);
  const int clockH = renderer.getLineHeight(SMALL_FONT_ID) + 8;
  if (timeBuf[0] != '\0') renderer.drawText(SMALL_FONT_ID, content.x + 16, content.y + 2, timeBuf);
  GUI.drawBatteryRight(renderer,
                       Rect{content.x + content.width - kHomeBatteryRightMargin - kHomeBatteryWidth, content.y + 2,
                            kHomeBatteryWidth, kHomeBatteryHeight},
                       SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS);

  const Rect panel{content.x + pad, content.y + clockH, content.width - pad * 2,
                   std::max(80, content.height - clockH - 4)};
  InxInkCards::drawCard(renderer, panel);

  renderer.drawText(UI_12_FONT_ID, panel.x + 12, panel.y + 8, tr(STR_NOW_READING), true, EpdFontFamily::BOLD);
  char countLine[32];
  snprintf(countLine, sizeof(countLine), tr(STR_BOOKS_READING_FMT), bookCount);
  renderer.drawText(SMALL_FONT_ID, panel.x + 12, panel.y + 8 + renderer.getLineHeight(UI_12_FONT_ID) + 1, countLine);

  const char* more = tr(STR_MORE);
  const int moreW = renderer.getTextWidth(UI_10_FONT_ID, more);
  moreHitRect_ =
      Rect{panel.x + panel.width - 14 - moreW, panel.y + 8, moreW + 10, renderer.getLineHeight(UI_10_FONT_ID) + 10};
  renderer.drawText(UI_10_FONT_ID, moreHitRect_.x, panel.y + 10, more);

  const int start = InxRecentGeometry::pageStart(selected, bookCount, InxRecentLayout::Flow);
  const int visible = std::min(InxRecentGeometry::itemsPerPage(InxRecentLayout::Flow), std::max(0, bookCount - start));

  if (visible == 1) {
    const int index = start;
    const int heroH = 260;
    const Rect heroPanel{content.x + pad, content.y + clockH, content.width - pad * 2, heroH};
    InxInkCards::drawCard(renderer, heroPanel);

    renderer.drawText(UI_12_FONT_ID, heroPanel.x + 14, heroPanel.y + 12, tr(STR_NOW_READING), true, EpdFontFamily::BOLD);

    const char* more = tr(STR_MORE);
    const int moreW = renderer.getTextWidth(UI_10_FONT_ID, more);
    moreHitRect_ = Rect{heroPanel.x + heroPanel.width - 14 - moreW, heroPanel.y + 10, moreW + 10,
                        renderer.getLineHeight(UI_10_FONT_ID) + 10};
    renderer.drawText(UI_10_FONT_ID, moreHitRect_.x, heroPanel.y + 12, more);

    // Book Cover (prominent hero aspect ratio)
    const int coverTop = heroPanel.y + 12 + renderer.getLineHeight(UI_12_FONT_ID) + 10;
    const int coverH = heroPanel.y + heroPanel.height - 18 - coverTop;
    const int coverW = coverH * 70 / 100;
    const Rect cover{heroPanel.x + 14, coverTop, coverW, coverH};
    setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(coverH));
    drawBookCover(index, cover);
    renderer.drawRect(cover.x, cover.y, cover.width, cover.height);

    // Text details on the right
    const int textX = cover.x + cover.width + 16;
    const int textW = heroPanel.x + heroPanel.width - 14 - textX;
    int ty = coverTop + 2;

    // Title (bold, up to 2 lines)
    const auto titleLines =
        renderer.wrappedText(titleFont, titleOf((*books)[index]), textW, 2, EpdFontFamily::BOLD);
    for (const auto& line : titleLines) {
      renderer.drawText(titleFont, textX, ty, line.c_str(), true, EpdFontFamily::BOLD);
      ty += titleLineH;
    }

    // Author
    if (!(*books)[index].author.empty()) {
      const std::string authorText = renderer.truncatedText(SMALL_FONT_ID, (*books)[index].author.c_str(), textW);
      renderer.drawText(SMALL_FONT_ID, textX, ty + 2, authorText.c_str());
      ty += renderer.getLineHeight(SMALL_FONT_ID) + 6;
    }

    // Progress text
    const ReadingBookStats* stats = statsAt(index);
    char progBuf[48] = {};
    if (stats && stats->completed && progressOf(stats) >= 99) {
      snprintf(progBuf, sizeof(progBuf), "%s", tr(STR_DONE));
    } else if (stats && !stats->chapterTitle.empty()) {
      const std::string cut = renderer.truncatedText(chapterFont, stats->chapterTitle.c_str(), textW);
      snprintf(progBuf, sizeof(progBuf), "%s (%u%%)", cut.c_str(), static_cast<unsigned>(progressOf(stats)));
    } else {
      snprintf(progBuf, sizeof(progBuf), "已读 %u%%", static_cast<unsigned>(progressOf(stats)));
    }
    renderer.drawText(chapterFont, textX, ty + 4, progBuf);
    ty += chapterH + 8;

    // Progress bar
    InxInkCards::drawProgress(renderer, Rect{textX, ty, textW, 6}, progressOf(stats));
    ty += 12;

    // Reading duration
    char durationBuf[32] = {};
    ReadingStatsAnalytics::formatDurationLabel(stats ? stats->totalReadingMs : 0, durationBuf, sizeof(durationBuf));
    char durLine[48] = {};
    snprintf(durLine, sizeof(durLine), "已读 %s", durationBuf);
    // "继续阅读" pill button at bottom right
    const char* continueText = "继续阅读 >";
    const int btnW = renderer.getTextWidth(SMALL_FONT_ID, continueText, EpdFontFamily::BOLD) + 16;
    const int btnH = renderer.getLineHeight(SMALL_FONT_ID) + 8;
    const int btnX = heroPanel.x + heroPanel.width - 14 - btnW;
    const int btnY = heroPanel.y + heroPanel.height - 14 - btnH;
    renderer.fillRoundedRect(btnX, btnY, btnW, btnH, 6, Color::White);
    renderer.drawRoundedRect(btnX, btnY, btnW, btnH, 1, 6, true);
    renderer.drawText(SMALL_FONT_ID, btnX + 8, btnY + 4, continueText, true, EpdFontFamily::BOLD);

    flowListTop_ = heroPanel.y;
    flowRowStep_ = heroPanel.height;
    flowVisible_ = 1;

    // Bottom Reading Overview section (fills the previously empty void!)
    const int statsY = heroPanel.y + heroPanel.height + 12;
    const int remainH = content.y + content.height - statsY - 6;
    if (remainH >= 80) {
      const int gap = 12;
      const int cardW = (content.width - pad * 2 - gap) / 2;
      const int cardH = std::min(105, (remainH - gap) / 2);

      // Card 1: Today
      char todayVal[24] = {};
      ReadingStatsAnalytics::formatDurationLabel(READING_STATS.getTodayReadingMs(), todayVal, sizeof(todayVal));
      InxInkCards::drawMetricCard(renderer, Rect{content.x + pad, statsY, cardW, cardH}, todayVal, tr(STR_TODAY_READING));

      // Card 2: Streak or 7-day total
      char streakVal[24] = {};
      const uint32_t streakDays = READING_STATS.getCurrentStreakDays();
      if (streakDays > 0) {
        snprintf(streakVal, sizeof(streakVal), "%u 天", streakDays);
        InxInkCards::drawMetricCard(renderer, Rect{content.x + pad + cardW + gap, statsY, cardW, cardH}, streakVal, "连续阅读");
      } else {
        ReadingStatsAnalytics::formatDurationLabel(READING_STATS.getRecentReadingMs(7), streakVal, sizeof(streakVal));
        InxInkCards::drawMetricCard(renderer, Rect{content.x + pad + cardW + gap, statsY, cardW, cardH}, streakVal, tr(STR_LAST_7D));
      }

      // Bottom Banner
      const int bannerY = statsY + cardH + gap;
      const int bannerH = remainH - cardH - gap;
      if (bannerH >= 50) {
        const Rect banner{content.x + pad, bannerY, content.width - pad * 2, bannerH};
        InxInkCards::drawCard(renderer, banner);

        char monthVal[24] = {};
        ReadingStatsAnalytics::formatDurationLabel(READING_STATS.getRecentReadingMs(30), monthVal, sizeof(monthVal));
        char monthSummary[64] = {};
        snprintf(monthSummary, sizeof(monthSummary), "本月已累计阅读 %s", monthVal);

        renderer.drawText(UI_10_FONT_ID, banner.x + 14, banner.y + 12, monthSummary, true, EpdFontFamily::BOLD);
        renderer.drawText(SMALL_FONT_ID, banner.x + 14, banner.y + 12 + renderer.getLineHeight(UI_10_FONT_ID) + 4,
                          "保持良好阅读习惯，享受安静阅读时光");
      }
    }
    return;
  }

  const int listTop = panel.y + 6 + headH;
  const int listH = std::max(1, panel.y + panel.height - 6 - listTop);
  const int rowStep = listH / 4;
  const int rowInset = 6;
  const int maxCoverH = std::max(52, rowStep - rowInset * 2);
  const int maxCoverW = std::min(panel.width * 32 / 100, maxCoverH);
  const auto coverSize = InxCoverGeometry::fit(maxCoverW, maxCoverH);
  const int coverW = coverSize.width;
  const int coverH = coverSize.height;
  flowListTop_ = listTop;
  flowRowStep_ = rowStep;
  flowVisible_ = visible;
  setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(coverH));

  for (int slot = 0; slot < visible; ++slot) {
    const int index = start + slot;
    const int y = listTop + slot * rowStep;
    if (slot > 0) renderer.drawLine(panel.x + 12, y, panel.x + panel.width - 12, y);
    const Rect cover{panel.x + 12, y + (rowStep - coverH) / 2, coverW, coverH};
    if (index == selected && showMainTabContentSelection()) {
      renderer.drawRect(panel.x + 8, y + 2, panel.width - 16, rowStep - 4);
    }
    drawBookCover(index, cover);
    renderer.drawRect(cover.x, cover.y, cover.width, cover.height);

    const int durCol = 62;
    const int textX = cover.x + cover.width + 12;
    const int textW = std::max(24, panel.x + panel.width - 12 - durCol - textX);
    const int progressH = 7;
    const int titleBudget = coverH - chapterH - progressH - 8;
    const int titleLinesMax = titleBudget >= titleLineH * 2 ? 2 : 1;
    const auto titleLines =
        renderer.wrappedText(titleFont, titleOf((*books)[index]), textW, titleLinesMax, EpdFontFamily::BOLD);
    int ty = cover.y + 2;
    for (const auto& line : titleLines) {
      renderer.drawText(titleFont, textX, ty, line.c_str(), true, EpdFontFamily::BOLD);
      ty += titleLineH;
    }

    const ReadingBookStats* stats = statsAt(index);
    char chLine[64] = {};
    if (stats && stats->completed) {
      snprintf(chLine, sizeof(chLine), "%s", tr(STR_DONE));
    } else if (stats && !stats->chapterTitle.empty()) {
      const std::string cut = renderer.truncatedText(chapterFont, stats->chapterTitle.c_str(), textW);
      snprintf(chLine, sizeof(chLine), "%s", cut.c_str());
    } else if (stats) {
      snprintf(chLine, sizeof(chLine), "%u%%", static_cast<unsigned>(stats->lastProgressPercent));
    }
    const int progressY = cover.y + coverH - progressH - 2;
    if (chLine[0] != '\0') {
      const int chY = std::min(ty + 4, progressY - chapterH - 4);
      renderer.drawText(chapterFont, textX, chY, chLine);
    }
    InxInkCards::drawHairProgress(renderer, Rect{textX, progressY, textW, progressH}, progressOf(stats));

    char number[8];
    bool hours = false;
    ReadingStatsAnalytics::formatDurationParts(stats ? stats->totalReadingMs : 0, number, sizeof(number), hours);
    const char* unit = hours ? tr(STR_HOURS_UNIT) : tr(STR_MINUTES_UNIT);
    const int numW = renderer.getTextWidth(UI_12_FONT_ID, number, EpdFontFamily::BOLD);
    const int unitW = renderer.getTextWidth(SMALL_FONT_ID, unit);
    const int durRight = panel.x + panel.width - 12;
    renderer.drawText(UI_12_FONT_ID, durRight - numW, cover.y + 2, number, true, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, durRight - unitW, cover.y + 2 + renderer.getLineHeight(UI_12_FONT_ID) + 2, unit);
  }
}

void InxRecentActivity::drawGrid(const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), layout());
  const int cellWidth = content.width / 2;
  const int cellHeight = content.height / 2;
  for (int slot = 0; slot < 4 && start + slot < static_cast<int>(books->size()); ++slot) {
    const int index = start + slot;
    const int column = slot % 2;
    const int row = slot / 2;
    const Rect cell{content.x + column * cellWidth + kGap / 2, content.y + row * cellHeight + kGap / 2,
                    cellWidth - kGap, cellHeight - kGap};
    if (showSelection && index == selected) drawSparseInk(renderer, cell);
    const Rect cover = fitCoverRect(Rect{cell.x + kGap, cell.y + kGap, cell.width - kGap * 2, cell.height - kGap * 2});
    if (slot == 0) setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(cover.height));
    drawBookCover(index, cover);
    if (showSelection && index == selected) drawThickFrame(renderer, cover);
    const int barWidth = std::max(24, cover.width - 30);
    const int barX = cover.x + (cover.width - barWidth) / 2;
    const int barY = cover.y + cover.height - 18;
    renderer.fillRect(barX - 2, barY - 2, barWidth + 4, kProgressHeight + 4, false);
    drawMiniProgress(renderer, Rect{barX, barY, barWidth, kProgressHeight}, progressOf(statsAt(index)));
  }
}

void InxRecentActivity::drawList(const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), layout());
  const int rowHeight = content.height / 5;
  for (int slot = 0; slot < 5 && start + slot < static_cast<int>(books->size()); ++slot) {
    const int index = start + slot;
    const Rect row{content.x, content.y + slot * rowHeight, content.width, rowHeight};
    if (showSelection && index == selected) drawSparseInk(renderer, row);
    const Rect cover = fitCoverRect(Rect{row.x + kPagePadding, row.y + 5, 88, row.height - 10});
    if (slot == 0) setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(cover.height));
    drawBookCover(index, cover);
    const int textX = cover.x + cover.width + 14;
    const int textWidth = row.x + row.width - kPagePadding - textX;
    const int textBottom = drawBookText(renderer, (*books)[index], textX, row.y + 10, textWidth, true);
    drawMiniProgress(renderer,
                     Rect{textX, std::max(textBottom + 6, row.y + row.height - 15), std::max(24, textWidth * 80 / 100),
                          kProgressHeight},
                     progressOf(statsAt(index)));
    if (slot + 1 < 5 && start + slot + 1 < static_cast<int>(books->size())) {
      drawDottedSeparator(renderer, row.x + kGap, row.y + row.height - 1, row.width - kGap * 2);
    }
  }
}

void InxRecentActivity::drawIcons(const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), layout());
  const int cellWidth = content.width / 3;
  const int cellHeight = content.height / 3;
  for (int slot = 0; slot < 9 && start + slot < static_cast<int>(books->size()); ++slot) {
    const int index = start + slot;
    const int column = slot % 3;
    const int row = slot / 3;
    const Rect cell{content.x + column * cellWidth + 5, content.y + row * cellHeight + 5, cellWidth - 10,
                    cellHeight - 10};
    const Rect cover = fitCoverRect(Rect{cell.x + 4, cell.y + 4, cell.width - 8, cell.height - 8});
    if (slot == 0) setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(cover.height));
    drawBookCover(index, cover);
    drawProgressBadge(renderer, cover, progressOf(statsAt(index)));
    if (showSelection && index == selected)
      drawThickFrame(renderer, Rect{cover.x - 2, cover.y - 2, cover.width + 4, cover.height + 4});
  }
}

void InxRecentActivity::drawCover(const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  constexpr int progressGap = 10;
  const int progressBlockHeight = progressGap + 8;
  const int targetWidth = std::max(1, content.width * 78 / 100);
  const Rect cover = fitCoverRect(Rect{content.x + (content.width - targetWidth) / 2, content.y + 6, targetWidth,
                                       std::max(1, content.height - progressBlockHeight - 12)});
  setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(cover.height));
  drawBookCover(selected, cover);
  if (showSelection) drawThickFrame(renderer, cover);
  const int barWidth = std::max(24, cover.width * 80 / 100);
  drawMiniProgress(renderer,
                   Rect{cover.x + (cover.width - barWidth) / 2, cover.y + cover.height + progressGap, barWidth, 8},
                   progressOf(statsAt(selected)));
}

void InxRecentActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  drawPageHeader(Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_MENU_RECENT_BOOKS));
  const Rect content = contentRect(renderer);

  if (!books || books->empty()) {
    char timeBuf[16] = {};
    TimeUtils::formatCurrentTime(timeBuf, sizeof(timeBuf), SETTINGS.clockFormat == 1);
    if (timeBuf[0] != '\0') renderer.drawText(SMALL_FONT_ID, content.x + 16, content.y + 4, timeBuf);
    GUI.drawBatteryRight(
        renderer,
        Rect{width - kHomeBatteryRightMargin - kHomeBatteryWidth, content.y + 2, kHomeBatteryWidth, kHomeBatteryHeight},
        SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS);
    UITheme::drawCenteredWrappedText(renderer, content, UI_12_FONT_ID, tr(STR_NO_RECENT_BOOKS), 2);
  } else {
    switch (layout()) {
      case InxRecentLayout::Flow:
        drawFlow(content);
        break;
      case InxRecentLayout::Grid:
        drawGrid(content);
        break;
      case InxRecentLayout::List:
        drawList(content);
        break;
      case InxRecentLayout::Icons:
        drawIcons(content);
        break;
      case InxRecentLayout::Cover:
        drawCover(content);
        break;
      case InxRecentLayout::Count:
        break;
    }
  }

  const auto labels = mainTabButtonLabels(SETTINGS.standbyShortcutEnabled ? tr(STR_STANDBY_TITLE) : "", tr(STR_OPEN),
                                          books && books->size() > 1, false);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (prepareNextMissingCover()) return;
  renderer.displayBuffer();
}
