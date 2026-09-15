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

  if (GUI.usesPaperStyle() && books && bookIndex >= 0 && bookIndex < static_cast<int>(books->size())) {
    const auto& book = (*books)[bookIndex];
    GUI.drawPaperCover(renderer, bounds, titleOf(book), book.author.c_str());
    return false;
  }
  const auto size = InxCoverGeometry::fit(bounds.width, bounds.height);
  const int x = bounds.x + (bounds.width - size.width) / 2;
  const int y = bounds.y + (bounds.height - size.height) / 2;
  renderer.fillRect(x, y, size.width, size.height, false);
  renderer.drawRect(x, y, size.width, size.height, 1, true);
  // Elegant embossed spine crease
  renderer.drawLine(x + 4, y, x + 4, y + size.height - 1, true);
  constexpr int iconSize = 28;
  renderer.drawIcon(CoverIcon, x + (size.width - iconSize) / 2, y + size.height / 3 - iconSize / 2, iconSize);
  if (books && bookIndex >= 0 && bookIndex < static_cast<int>(books->size())) {
    const std::string title = renderer.truncatedText(SMALL_FONT_ID, (*books)[bookIndex].title.c_str(), size.width - 10);
    const int tw = renderer.getTextWidth(SMALL_FONT_ID, title.c_str());
    renderer.drawText(SMALL_FONT_ID, x + (size.width - tw) / 2, y + size.height * 2 / 3, title.c_str());
  }
  return false;
}

bool InxRecentActivity::prepareNextMissingCover() {
  if (!books || thumbnailHeight <= 0) return false;
  const int bookCount = static_cast<int>(std::min(books->size(), RecentBooksStore::MAX_RECENT_BOOKS));
  // Paper Flow always shows only the first book's cover, even when a recent row has focus.
  const bool paperFlow = GUI.usesPaperStyle() && layout() == InxRecentLayout::Flow;
  const int start = paperFlow ? 0 : InxRecentGeometry::pageStart(selected, bookCount, layout());
  const int visible =
      std::min(paperFlow ? 1 : InxRecentGeometry::itemsPerPage(layout()), std::max(0, bookCount - start));
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
      if (x >= flowHeroHit_.x && x < flowHeroHit_.x + flowHeroHit_.width &&
          y >= flowHeroHit_.y && y < flowHeroHit_.y + flowHeroHit_.height) {
        return 0;
      }
      for (size_t i = 1; i < RecentBooksStore::MAX_RECENT_BOOKS; ++i) {
        if (x >= flowShelfHit_[i].x && x < flowShelfHit_[i].x + flowShelfHit_[i].width &&
            y >= flowShelfHit_[i].y && y < flowShelfHit_[i].y + flowShelfHit_[i].height) {
          return i < books->size() ? static_cast<int>(i) : -1;
        }
      }
      return -1;
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
  if (!books || books->empty()) {
    int x = 0;
    int y = 0;
    if (GUI.usesPaperStyle() && mappedInput.wasScreenTapped(x, y) && x >= moreHitRect_.x &&
        x < moreHitRect_.x + moreHitRect_.width && y >= moreHitRect_.y && y < moreHitRect_.y + moreHitRect_.height) {
      activityManager.goToMainTab(MainTab::Library);
    }
    return;
  }

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
    if (currentLayout == InxRecentLayout::Flow) {
      if (GUI.usesPaperStyle()) {
        const int capacity = GUI.paperHomeLayout(renderer, contentRect(renderer)).shelfCapacity;
        if (mappedInput.wasReleased(MappedInputManager::Button::NavNext)) {
          selected = (selected + 1) % count;
          flowPage_ = selected > 0 ? (selected - 1) / capacity : 0;
        } else if ((flowPage_ + 1) * capacity < count - 1) {
          ++flowPage_;
          selected = 1 + flowPage_ * capacity;
        }
        requestUpdate();
        return;
      }
      const int totalShelf = std::max(0, count - 1);
      if ((flowPage_ + 1) * 4 < totalShelf) {
        flowPage_++;
        requestUpdate();
      }
      return;
    }
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
    if (currentLayout == InxRecentLayout::Flow) {
      if (GUI.usesPaperStyle()) {
        const int capacity = GUI.paperHomeLayout(renderer, contentRect(renderer)).shelfCapacity;
        if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
          selected = (selected + count - 1) % count;
          flowPage_ = selected > 0 ? (selected - 1) / capacity : 0;
        } else if (flowPage_ > 0) {
          --flowPage_;
          selected = 1 + flowPage_ * capacity;
        }
        requestUpdate();
        return;
      }
      if (flowPage_ > 0) {
        flowPage_--;
        requestUpdate();
      }
      return;
    }
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
      activityManager.goToMainTab(GUI.usesPaperStyle() ? MainTab::Library : MainTab::Statistics);
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
  if (GUI.usesPaperStyle()) {
    drawPaperFlow(content);
    return;
  }
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

  // 1. Hero Book Showcase (Book 0 - Currently Reading)
  const int heroH = 264;
  const Rect heroPanel{content.x + pad, content.y + clockH, content.width - pad * 2, heroH};
  InxInkCards::drawCard(renderer, heroPanel, 8);
  flowHeroHit_ = heroPanel;

  renderer.drawText(UI_12_FONT_ID, heroPanel.x + 14, heroPanel.y + 12, tr(STR_NOW_READING), true, EpdFontFamily::BOLD);

  const char* more = tr(STR_MORE);
  const int moreW = renderer.getTextWidth(UI_10_FONT_ID, more);
  moreHitRect_ = Rect{heroPanel.x + heroPanel.width - 14 - moreW, heroPanel.y + 10, moreW + 10,
                      renderer.getLineHeight(UI_10_FONT_ID) + 10};
  renderer.drawText(UI_10_FONT_ID, moreHitRect_.x, heroPanel.y + 12, more);

  // Book 0 Cover
  const int coverTop = heroPanel.y + 12 + renderer.getLineHeight(UI_12_FONT_ID) + 10;
  const int coverH = heroPanel.y + heroPanel.height - 18 - coverTop;
  const int coverW = coverH * 70 / 100;
  const Rect cover{heroPanel.x + 14, coverTop, coverW, coverH};
  setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(coverH));
  drawBookCover(0, cover);
  renderer.drawRect(cover.x, cover.y, cover.width, cover.height, 1, true);
  renderer.drawLine(cover.x + 4, cover.y, cover.x + 4, cover.y + cover.height - 1, 1, true);

  // Text details on the right
  const int textX = cover.x + cover.width + 16;
  const int textW = heroPanel.x + heroPanel.width - 14 - textX;
  int ty = coverTop + 2;

  const auto titleLines =
      renderer.wrappedText(titleFont, titleOf((*books)[0]), textW, 2, EpdFontFamily::BOLD);
  for (const auto& line : titleLines) {
    renderer.drawText(titleFont, textX, ty, line.c_str(), true, EpdFontFamily::BOLD);
    ty += titleLineH;
  }

  if (!(*books)[0].author.empty()) {
    const std::string authorText = renderer.truncatedText(SMALL_FONT_ID, (*books)[0].author.c_str(), textW);
    renderer.drawText(SMALL_FONT_ID, textX, ty + 2, authorText.c_str());
    ty += renderer.getLineHeight(SMALL_FONT_ID) + 6;
  }

  const ReadingBookStats* stats0 = statsAt(0);
  char progBuf[48] = {};
  if (stats0 && stats0->completed && progressOf(stats0) >= 99) {
    snprintf(progBuf, sizeof(progBuf), "%s", tr(STR_DONE));
  } else if (stats0 && !stats0->chapterTitle.empty()) {
    const std::string cut = renderer.truncatedText(chapterFont, stats0->chapterTitle.c_str(), textW);
    snprintf(progBuf, sizeof(progBuf), "%s (%u%%)", cut.c_str(), static_cast<unsigned>(progressOf(stats0)));
  } else {
    snprintf(progBuf, sizeof(progBuf), "已读 %u%%", static_cast<unsigned>(progressOf(stats0)));
  }
  renderer.drawText(chapterFont, textX, ty + 4, progBuf);
  ty += chapterH + 8;

  InxInkCards::drawProgress(renderer, Rect{textX, ty, textW, 6}, progressOf(stats0));
  ty += 12;

  const char* continueText = "继续阅读 >";
  const int btnW = renderer.getTextWidth(SMALL_FONT_ID, continueText, EpdFontFamily::BOLD) + 20;
  const int btnH = renderer.getLineHeight(SMALL_FONT_ID) + 10;
  const int btnX = heroPanel.x + heroPanel.width - 14 - btnW;
  const int btnY = heroPanel.y + heroPanel.height - 14 - btnH;
  renderer.fillRoundedRect(btnX, btnY, btnW, btnH, 6, Color::Black);
  renderer.drawText(SMALL_FONT_ID, btnX + 10, btnY + 5, continueText, false, EpdFontFamily::BOLD);

  flowListTop_ = heroPanel.y;
  flowRowStep_ = heroPanel.height;
  flowVisible_ = 1;

  // 2. Recent Reading Shelf ("最近阅读" list with multi-page support)
  const int recentY = heroPanel.y + heroPanel.height + 12;
  const int recentW = content.width - pad * 2;
  renderer.drawText(UI_10_FONT_ID, content.x + pad, recentY, "最近阅读", true, EpdFontFamily::BOLD);

  const int totalShelf = std::max(0, bookCount - 1);
  const int totalPages = std::max(1, (totalShelf + 3) / 4);
  if (flowPage_ >= totalPages) flowPage_ = totalPages - 1;
  if (flowPage_ < 0) flowPage_ = 0;

  char totalBooksBuf[32] = {};
  if (totalPages > 1) {
    snprintf(totalBooksBuf, sizeof(totalBooksBuf), "%d/%d 页 · 共 %d 本", flowPage_ + 1, totalPages, bookCount);
  } else {
    snprintf(totalBooksBuf, sizeof(totalBooksBuf), "共 %d 本", bookCount);
  }
  const int tbw = renderer.getTextWidth(SMALL_FONT_ID, totalBooksBuf);
  renderer.drawText(SMALL_FONT_ID, content.x + pad + recentW - tbw, recentY + 1, totalBooksBuf);

  int itemY = recentY + renderer.getLineHeight(UI_10_FONT_ID) + 6;
  renderer.drawLine(content.x + pad, itemY, content.x + pad + recentW, itemY, 1, true);
  itemY += 6;

  for (size_t i = 0; i < RecentBooksStore::MAX_RECENT_BOOKS; ++i) {
    flowShelfHit_[i] = Rect(0, 0, 0, 0);
  }

  const int rowH = 82;
  const int shelfStart = 1 + flowPage_ * 4;
  const int maxRecent = std::min(bookCount, shelfStart + 4);
  for (int i = shelfStart; i < maxRecent; ++i) {
    const auto& b = (*books)[i];
    const int spineW = 32;
    const int spineH = 48;
    flowShelfHit_[i] = Rect{content.x + pad, itemY, content.width - pad * 2, rowH};

    // 1. Mini Book Jacket / Spine (with 5px solid black spine crease)
    const Rect spine{content.x + pad, itemY + (rowH - spineH) / 2, spineW, spineH};
    renderer.fillRoundedRect(spine.x, spine.y, spine.width, spine.height, 3, Color::White);
    renderer.drawRoundedRect(spine.x, spine.y, spine.width, spine.height, 1, 3, true);
    renderer.fillRect(spine.x, spine.y, 5, spine.height, true);

    // 2. Text info on the right
    const int infoX = spine.x + spineW + 14;
    const ReadingBookStats* stats = statsAt(i);
    const uint8_t pct = progressOf(stats);
    char pctBuf[16] = {};
    if (stats && stats->completed && pct >= 99) {
      snprintf(pctBuf, sizeof(pctBuf), "完");
    } else {
      snprintf(pctBuf, sizeof(pctBuf), "%u%%", pct);
    }
    const int pctW = renderer.getTextWidth(UI_12_FONT_ID, pctBuf, EpdFontFamily::BOLD);
    const int infoW = content.x + pad + recentW - pctW - 12 - infoX;

    const int titleY = itemY + 12;
    const std::string bTitle = renderer.truncatedText(UI_12_FONT_ID, titleOf(b), infoW, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, infoX, titleY, bTitle.c_str(), true, EpdFontFamily::BOLD);

    std::string meta;
    if (stats && !stats->chapterTitle.empty()) {
      meta = renderer.truncatedText(SMALL_FONT_ID, stats->chapterTitle.c_str(), infoW);
    } else if (!b.author.empty()) {
      meta = renderer.truncatedText(SMALL_FONT_ID, b.author.c_str(), infoW);
    }
    if (!meta.empty()) {
      const int metaY = titleY + renderer.getLineHeight(UI_12_FONT_ID) + 6;
      renderer.drawText(SMALL_FONT_ID, infoX, metaY, meta.c_str());
    }

    // 3. Percentage on right
    renderer.drawText(UI_12_FONT_ID, content.x + pad + recentW - pctW, itemY + (rowH - renderer.getLineHeight(UI_12_FONT_ID)) / 2, pctBuf, true, EpdFontFamily::BOLD);

    itemY += rowH;
    if (i < maxRecent - 1) {
      drawDottedSeparator(renderer, content.x + pad, itemY - 2, recentW);
    }
  }
}

void InxRecentActivity::drawPaperFlow(const Rect& content) {
  const auto& m = GUI.paperMetrics();
  const auto layout = GUI.paperHomeLayout(renderer, content);
  const int count = static_cast<int>(books->size());
  const int focus = selected;
  const bool showFocus = showMainTabContentSelection();
  const int smallH = renderer.getLineHeight(m.smallFont);
  const int bodyH = renderer.getLineHeight(m.bodyFont);
  const int titleH = renderer.getLineHeight(m.bookFont);
  const GfxRenderer::ClipScope clip(renderer, content.x, content.y, content.width, content.height);
  GUI.drawPaperStatus(renderer, layout.status);
  char date[24] = {};
  std::tm local{};
  if (TimeUtils::getLocalDateTime(TimeUtils::getCurrentValidTimestamp(), local)) {
    snprintf(date, sizeof(date), "%02d.%02d", local.tm_mon + 1, local.tm_mday);
  }
  GUI.drawPaperHeading(renderer, layout.kicker, tr(STR_NOW_READING), date);
  flowHeroHit_ = layout.hero;
  heroCoverRect_ = layout.cover;
  setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(layout.cover.height));
  drawBookCover(0, layout.cover);

  const RecentBook& book = (*books)[0];
  const ReadingBookStats* stats = statsAt(0);
  const Rect detail = layout.details;
  const int actionHeight = std::max(44, bodyH + 12);
  const int actionLimit = detail.y + detail.height - actionHeight;
  int y = GUI.drawPaperText(renderer, Rect{detail.x, detail.y, detail.width, titleH * 3}, m.bookFont, titleOf(book),
                            true, 3);
  y += 8;
  if (!book.author.empty()) {
    y = GUI.drawPaperText(renderer, Rect{detail.x, y, detail.width, smallH}, m.smallFont, book.author.c_str()) + 12;
  }
  if (stats && !stats->chapterTitle.empty() && y + bodyH + smallH + 22 <= actionLimit) {
    y = GUI.drawPaperText(renderer, Rect{detail.x, y, detail.width, smallH}, m.smallFont, stats->chapterTitle.c_str()) +
        12;
  }
  if (y + smallH + 15 <= actionLimit) {
    GUI.drawPaperProgress(renderer, Rect{detail.x, y, detail.width, 3}, progressOf(stats));
    char progress[16] = {};
    snprintf(progress, sizeof(progress), "%u%%", static_cast<unsigned>(progressOf(stats)));
    GUI.drawPaperText(renderer, Rect{detail.x, y + 9, detail.width, smallH}, m.smallFont,
                      stats && stats->completed ? tr(STR_DONE) : progress);
  }
  const int actionY = std::min(actionLimit, y + smallH + 22);
  GUI.drawPaperAction(renderer, Rect{detail.x, actionY, detail.width, actionHeight},
                      stats && stats->completed ? tr(STR_PAPER_REOPEN) : tr(STR_CONTINUE_READING),
                      showFocus && focus == 0, true);

  if (layout.note.height > 0) {
    char summary[96] = {};
    if (stats && stats->completed) {
      snprintf(summary, sizeof(summary), "%s", tr(STR_PAPER_BOOK_FINISHED));
    } else if (stats && stats->totalReadingMs > 0) {
      unsigned days = 0;
      for (const auto& day : stats->readingDays)
        if (day.readingMs > 0) ++days;
      snprintf(summary, sizeof(summary), tr(STR_PAPER_BOOK_BRIEF_FMT), days,
               static_cast<unsigned long long>((stats->totalReadingMs + 30000) / 60000));
    } else {
      snprintf(summary, sizeof(summary), "%s", tr(STR_PAPER_BOOK_NEW));
    }
    GUI.drawPaperBookmark(renderer, Rect{layout.note.x, layout.note.y + 8, 11, 17});
    GUI.drawPaperText(renderer, Rect{layout.note.x + 20, layout.note.y, layout.note.width - 20, titleH}, m.bookFont,
                      summary);
  }

  const int capacity = layout.shelfCapacity;
  const int totalPages = std::max(1, (count - 1 + capacity - 1) / capacity);
  flowPage_ = std::clamp(flowPage_, 0, totalPages - 1);
  const int sectionFont = m.bodyFont;
  const int sectionH = renderer.getLineHeight(sectionFont);
  const int moreWidth =
      std::min(layout.shelfHeading.width / 3, renderer.getTextWidth(sectionFont, tr(STR_PAPER_LIBRARY)) + 28);
  moreHitRect_ = Rect{layout.shelfHeading.x + layout.shelfHeading.width - moreWidth, layout.shelfHeading.y, moreWidth,
                      std::max(44, layout.shelfHeading.height)};
  char section[64] = {};
  snprintf(section, sizeof(section), tr(STR_PAPER_RECENT_COUNT_FMT), static_cast<unsigned>(count));
  GUI.drawPaperBar(renderer, Rect{layout.shelfHeading.x, layout.shelfHeading.y + 4, 4, sectionH - 8}, true);
  GUI.drawPaperText(
      renderer,
      Rect{layout.shelfHeading.x + 12, layout.shelfHeading.y, layout.shelfHeading.width - moreWidth - 24, sectionH},
      sectionFont, count > 1 ? section : tr(STR_PAPER_FIRST_BOOK), true);
  GUI.drawPaperText(renderer, Rect{moreHitRect_.x, moreHitRect_.y, moreWidth, sectionH}, sectionFont,
                    tr(STR_PAPER_LIBRARY));
  for (auto& hit : flowShelfHit_) hit = Rect{};
  const int start = 1 + flowPage_ * capacity;
  const int visible = std::max(1, std::min(capacity, count - start));
  const int rowHeight = std::min(layout.shelf.height / visible, std::max(104, titleH + smallH + 20));
  for (int slot = 0; slot < capacity && start + slot < count; ++slot) {
    const int index = start + slot;
    const auto& recent = (*books)[index];
    const auto* recentStats = statsAt(index);
    const Rect row{layout.shelf.x, layout.shelf.y + slot * rowHeight, layout.shelf.width, rowHeight};
    flowShelfHit_[index] = row;
    char number[8] = {};
    snprintf(number, sizeof(number), "%02d", index);
    GUI.drawPaperText(renderer, Rect{row.x + 8, row.y + 8, 36, renderer.getLineHeight(m.bodyFont)}, m.bodyFont, number);
    char progress[16] = {};
    snprintf(progress, sizeof(progress), "%u%%", static_cast<unsigned>(progressOf(recentStats)));
    const int progressW = renderer.getTextWidth(m.smallFont, progress);
    const int textX = row.x + 52;
    const int textW = row.width - 64 - progressW;
    GUI.drawPaperText(renderer, Rect{textX, row.y + 8, textW, titleH}, m.bookFont, titleOf(recent), true);
    GUI.drawPaperText(renderer, Rect{textX, row.y + titleH + 12, textW, smallH}, m.smallFont, recent.author.c_str());
    GUI.drawPaperText(renderer, Rect{row.x + row.width - progressW, row.y + 16, progressW, smallH}, m.smallFont,
                      progress);
    GUI.drawPaperRule(renderer, Rect{row.x, row.y + row.height - 1, row.width, 1});
    if (showFocus && focus == index) GUI.drawPaperFocus(renderer, row);
  }
  flowListTop_ = layout.shelf.y;
  flowRowStep_ = rowHeight;
  flowVisible_ = capacity;
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

  if ((!books || books->empty()) && GUI.usesPaperStyle()) {
    const auto& m = GUI.paperMetrics();
    GUI.drawPaperStatus(renderer, Rect{content.x, content.y, content.width, m.statusHeight});
    const int textY = content.y + content.height / 3;
    GUI.drawPaperText(renderer, Rect{content.x + m.padding, textY, content.width - m.padding * 2, 100}, m.titleFont,
                      tr(STR_PAPER_HOME_EMPTY), true, 2);
    GUI.drawPaperText(renderer, Rect{content.x + m.padding, textY + 108, content.width - m.padding * 2, 80}, m.bodyFont,
                      tr(STR_PAPER_HOME_EMPTY_DESC), false, 2);
    moreHitRect_ = Rect{content.x + m.padding, textY + 200, content.width - m.padding * 2, 48};
    GUI.drawPaperAction(renderer, moreHitRect_, tr(STR_PAPER_LIBRARY));
  } else if (!books || books->empty()) {
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
