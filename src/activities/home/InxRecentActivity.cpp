#include "InxRecentActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "BookTitleFont.h"
#include "CrossPointSettings.h"
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

namespace {
constexpr int kGap = 8;
constexpr int kPagePadding = 18;
constexpr int kProgressHeight = 6;
constexpr int kHomeBatteryWidth = 15;
constexpr int kHomeBatteryHeight = 12;
constexpr int kHomeBatteryRightMargin = 12;

Rect contentRect(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  return Rect{0, top, renderer.getScreenWidth(),
              renderer.getScreenHeight() - top - metrics.buttonHintsHeight - metrics.verticalSpacing};
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
  // Reader font when it covers common CJK; otherwise the full 12pt subset
  // (built-in 14/16/18 hold UI-string glyphs only). See BookTitleFont.h.
  return bookTitleFontId();
}

int drawBookText(const GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                 const bool author) {
  const int titleFont = titleFontId();
  const auto titleLines = renderer.wrappedText(titleFont, titleOf(book), width, 2, EpdFontFamily::BOLD);
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
  // Entering swaps the whole screen from another tab; a FAST differential
  // refresh leaves the previous tab's ink ghosted (faint tab icons, card
  // fills). Enter clean (HALF); later in-place repaints stay FAST.
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
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
  const size_t count = std::min(books->size(), RecentBooksStore::MAX_RECENT_BOOKS);
  for (size_t index = 0; index < count; ++index) {
    CoverCacheState& state = targetCoverStates[index];
    switch (state) {
      case CoverCacheState::Unchecked: {
        const std::string& templatePath = (*books)[index].coverBmpPath;
        if (templatePath.empty()) {
          state = CoverCacheState::Unavailable;
          continue;
        }
        state = Storage.exists(UITheme::getCoverThumbPath(templatePath, thumbnailHeight).c_str())
                    ? CoverCacheState::Ready
                    : CoverCacheState::Missing;
        if (state == CoverCacheState::Ready) continue;
        break;
      }
      case CoverCacheState::Ready:
      case CoverCacheState::Unavailable:
        continue;
      case CoverCacheState::Missing:
        break;
    }

    renderer.displayBuffer();
    {
      GfxRenderer::FrameBufferLoan loan(renderer);
      state = BookCoverLoader::ensureThumbnail((*books)[index].path, thumbnailHeight).empty()
                  ? CoverCacheState::Unavailable
                  : CoverCacheState::Ready;
    }
    requestUpdate();
    return true;
  }
  return false;
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
    case InxRecentLayout::Flow:
      if (x < heroCoverRect_.x || y < heroCoverRect_.y || x >= heroCoverRect_.x + heroCoverRect_.width ||
          y >= heroCoverRect_.y + heroCoverRect_.height) {
        return -1;
      }
      return selected;
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
  const auto swipe = mappedInput.wasSwipe();
  if (mappedInput.wasReleased(MappedInputManager::Button::NavNext) || swipe == MappedInputManager::SwipeDir::Up ||
      swipe == MappedInputManager::SwipeDir::Left) {
    if (count > 1) {
      selected = (selected + 1) % count;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious) || swipe == MappedInputManager::SwipeDir::Down ||
      swipe == MappedInputManager::SwipeDir::Right) {
    if (count > 1) {
      selected = (selected + count - 1) % count;
      requestUpdate();
    }
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTouchDown(x, y)) {
    const int touched = indexFromPoint(x, y);
    if (touched >= 0 && touched != selected) {
      selected = touched;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasScreenTapped(x, y)) {
    const int touched = indexFromPoint(x, y);
    if (touched >= 0) {
      selected = touched;
      openSelected();
    }
  }
}

void InxRecentActivity::drawFlow(const Rect& content) {
  const RecentBook& book = (*books)[selected];
  const int pad = InxInkCards::kPagePad;
  const int titleH = InxInkCards::pageTitleHeight(renderer);
  InxInkCards::drawPageTitle(renderer, content.x + pad, content.y + 8, tr(STR_MENU_RECENT_BOOKS));

  const int bookCount = books ? static_cast<int>(books->size()) : 0;
  const int dotH = bookCount > 1 ? 16 : 0;
  // 2x2 metric cells must fit value+label at their real line heights.
  const int gridH =
      std::max(2 * InxInkCards::metricCardMinHeight(renderer) + InxInkCards::kGap, (content.height - dotH) * 34 / 100);
  const Rect grid{content.x + pad, content.y + content.height - dotH - gridH, content.width - pad * 2, gridH};
  const Rect hero{content.x + pad, content.y + 8 + titleH + 10, content.width - pad * 2,
                  std::max(120, grid.y - InxInkCards::kGap - (content.y + 8 + titleH + 10))};
  InxInkCards::drawCard(renderer, hero);

  const int heroPad = 14;
  const auto coverSize = InxCoverGeometry::fit(hero.width * 38 / 100, hero.height - heroPad * 2);
  heroCoverRect_ =
      Rect{hero.x + heroPad, hero.y + (hero.height - coverSize.height) / 2, coverSize.width, coverSize.height};
  setThumbnailHeight(InxCoverGeometry::thumbnailHeightForCropFill(heroCoverRect_.height));
  drawBookCover(selected, heroCoverRect_);
  renderer.drawRect(heroCoverRect_.x, heroCoverRect_.y, heroCoverRect_.width, heroCoverRect_.height);

  const int textX = heroCoverRect_.x + heroCoverRect_.width + 16;
  const int textW = hero.x + hero.width - heroPad - textX;
  const int titleFont = titleFontId();
  const auto titleLines = renderer.wrappedText(titleFont, titleOf(book), textW, 2, EpdFontFamily::BOLD);
  int textY = hero.y + heroPad + 4;
  const int titleLineH = renderer.getLineHeight(titleFont);
  for (const auto& line : titleLines) {
    renderer.drawText(titleFont, textX, textY, line.c_str(), true, EpdFontFamily::BOLD);
    textY += titleLineH;
  }
  if (!book.author.empty()) {
    textY += 6;
    const std::string author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textW);
    renderer.drawText(UI_10_FONT_ID, textX, textY, author.c_str());
  }

  const ReadingBookStats* stats = statsAt(selected);
  const uint8_t progress = progressOf(stats);
  char percent[8];
  snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(progress));
  const int pctW = renderer.getTextWidth(SMALL_FONT_ID, percent, EpdFontFamily::BOLD);
  const int barY = hero.y + hero.height - heroPad - InxInkCards::kProgressHeight - 2;
  InxInkCards::drawProgress(renderer, Rect{textX, barY, std::max(24, textW - pctW - 10), InxInkCards::kProgressHeight},
                            progress);
  renderer.drawText(SMALL_FONT_ID, textX + textW - pctW, barY - 2, percent, true, EpdFontFamily::BOLD);

  const std::string total = stats ? ReadingStatsAnalytics::formatDurationHm(stats->totalReadingMs) : "0m";
  const std::string last = stats ? ReadingStatsAnalytics::formatDurationHm(stats->lastSessionMs) : "0m";
  char sessions[16];
  char chapter[8];
  snprintf(sessions, sizeof(sessions), "%u", stats ? static_cast<unsigned>(stats->sessions) : 0U);
  snprintf(chapter, sizeof(chapter), "%u%%", stats ? static_cast<unsigned>(stats->chapterProgressPercent) : 0U);
  const char* values[] = {total.c_str(), sessions, last.c_str(), chapter};
  const char* labels[] = {tr(STR_TOTAL_TIME), tr(STR_SESSIONS), tr(STR_LAST_SESSION), tr(STR_CHAPTER_PROGRESS)};
  for (int i = 0; i < 4; ++i) {
    Rect cell{};
    InxInkCards::layoutGrid(grid, 2, 2, i, cell);
    InxInkCards::drawMetricCard(renderer, cell, values[i], labels[i]);
  }

  if (bookCount > 1) {
    drawPaginationDots(renderer, renderer.getScreenWidth(), content.y + content.height - 10, bookCount, selected);
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
  GUI.drawBatteryRight(renderer,
                       Rect{renderer.getScreenWidth() - kHomeBatteryRightMargin - kHomeBatteryWidth,
                            renderer.getScreenHeight() - 30, kHomeBatteryWidth, kHomeBatteryHeight},
                       SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS);
  if (prepareNextMissingCover()) return;
  renderer.displayBuffer();
}
