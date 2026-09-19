#include "ReadingStatsDetailActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "AppMetricCard.h"
#include "BookReadingAdjustmentActivity.h"
#include "InxItemLayout.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "components/themes/inx/InxInkCards.h"
#include "components/icons/settings2.h"
#include "fontIds.h"
#include "util/BookCoverLoader.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace {
constexpr int COVER_WIDTH = 96;
constexpr int COVER_HEIGHT = 140;
constexpr int PROGRESS_BLOCK_HEIGHT = 38;
constexpr int METRIC_CARD_HEIGHT = 70;
constexpr int METRIC_CARD_GAP = 8;
constexpr int DETAIL_FOCUS_ITEM_COUNT = 2;
constexpr int DETAIL_ADJUST_FOCUS_INDEX = 1;
constexpr int ADJUST_BUTTON_SIZE = 54;
constexpr int SUMMARY_BANNER_HEIGHT = 46;
constexpr int SUMMARY_BANNER_GAP = 8;
constexpr int DETAIL_SCROLL_STEP = 128;
constexpr size_t MAX_RESOLVED_COVERS = 16;
constexpr uint64_t MIN_ESTIMATE_READING_MS = 10ULL * 60ULL * 1000ULL;
constexpr uint8_t MIN_ESTIMATE_PROGRESS_PERCENT = 5;
constexpr uint64_t MIN_ESTIMATE_AVG_SESSION_MS = 5ULL * 60ULL * 1000ULL;
constexpr uint64_t ESTIMATE_ROUNDING_MS = 5ULL * 60ULL * 1000ULL;

struct ResolvedCoverCacheEntry {
  std::string bookPath;
  std::string coverBmpPath;
  std::string resolvedPath;
};

std::vector<ResolvedCoverCacheEntry>& getResolvedCoverCache() {
  static std::vector<ResolvedCoverCacheEntry> cache;
  return cache;
}

std::string getCachedResolvedCoverPath(const ReadingBookStats& book) {
  auto& cache = getResolvedCoverCache();
  for (auto it = cache.begin(); it != cache.end(); ++it) {
    if (it->bookPath != book.path || it->coverBmpPath != book.coverBmpPath) {
      continue;
    }
    if (!it->resolvedPath.empty() && Storage.exists(it->resolvedPath.c_str())) {
      if (it != cache.begin()) {
        ResolvedCoverCacheEntry entry = *it;
        cache.erase(it);
        cache.insert(cache.begin(), std::move(entry));
      }
      return cache.front().resolvedPath;
    }
    break;
  }
  return "";
}

void rememberResolvedCoverPath(const ReadingBookStats& book, const std::string& resolvedPath) {
  if (resolvedPath.empty()) {
    return;
  }

  auto& cache = getResolvedCoverCache();
  cache.erase(std::remove_if(cache.begin(), cache.end(),
                             [&](const ResolvedCoverCacheEntry& entry) { return entry.bookPath == book.path; }),
              cache.end());
  cache.insert(cache.begin(), ResolvedCoverCacheEntry{book.path, book.coverBmpPath, resolvedPath});
  if (cache.size() > MAX_RESOLVED_COVERS) {
    cache.pop_back();
  }
}

ReadingBookStats withCoverPath(const ReadingBookStats& book, const std::string& coverBmpPath) {
  ReadingBookStats updated = book;
  updated.coverBmpPath = coverBmpPath;
  return updated;
}

const ReadingBookStats* findBook(const std::string& bookPath) {
  for (const auto& book : READING_STATS.getBooks()) {
    // cppcheck-suppress useStlAlgorithm
    if (book.path == bookPath) {
      return &book;
    }
  }
  return nullptr;
}

std::string resolveStoredCoverPath(const std::string& coverBmpPath) {
  if (coverBmpPath.empty()) {
    return "";
  }

  if (coverBmpPath.find("[HEIGHT]") != std::string::npos) {
    const int candidateHeights[] = {COVER_HEIGHT, 160, 240, 400};
    for (const int height : candidateHeights) {
      const std::string resolved = UITheme::getCoverThumbPath(coverBmpPath, height);
      if (Storage.exists(resolved.c_str())) {
        return resolved;
      }
    }
    return "";
  }

  return Storage.exists(coverBmpPath.c_str()) ? coverBmpPath : "";
}

std::string ensureCoverPath(const ReadingBookStats& book) {
  const std::string cachedResolvedPath = getCachedResolvedCoverPath(book);
  if (!cachedResolvedPath.empty()) {
    return cachedResolvedPath;
  }

  std::string resolved = resolveStoredCoverPath(book.coverBmpPath);
  if (!resolved.empty()) {
    rememberResolvedCoverPath(book, resolved);
    return resolved;
  }

  std::string title;
  std::string author;
  const std::string coverPath = BookCoverLoader::ensureFullCover(book.path, &title, &author);
  if (coverPath.empty()) return "";

  READING_STATS.updateBookMetadata(book.path, title, author, coverPath);
  rememberResolvedCoverPath(withCoverPath(book, coverPath), coverPath);
  return coverPath;
}

std::string findFastCoverPath(const ReadingBookStats& book) {
  const std::string cachedResolvedPath = getCachedResolvedCoverPath(book);
  if (!cachedResolvedPath.empty()) {
    return cachedResolvedPath;
  }

  std::string resolved = resolveStoredCoverPath(book.coverBmpPath);
  if (!resolved.empty()) {
    rememberResolvedCoverPath(book, resolved);
    return resolved;
  }

  if (!Storage.exists(book.path.c_str())) {
    return "";
  }

  if (FsHelpers::hasEpubExtension(book.path)) {
    Epub epub(book.path, "/.crosspoint");
    resolved = resolveStoredCoverPath(epub.getCoverBmpPath());
  } else if (FsHelpers::hasXtcExtension(book.path)) {
    Xtc xtc(book.path, "/.crosspoint");
    resolved = resolveStoredCoverPath(xtc.getCoverBmpPath());
  } else if (FsHelpers::hasTxtExtension(book.path) || FsHelpers::hasMarkdownExtension(book.path)) {
    Txt txt(book.path, "/.crosspoint");
    resolved = resolveStoredCoverPath(txt.getCoverBmpPath());
  }

  if (!resolved.empty()) {
    READING_STATS.updateBookMetadata(book.path, "", "", resolved);
    rememberResolvedCoverPath(withCoverPath(book, resolved), resolved);
  }
  return resolved;
}

std::string getDisplayTitle(const ReadingBookStats& book) { return book.title.empty() ? book.path : book.title; }

std::string formatDate(const uint32_t timestamp) {
  const std::string formatted = TimeUtils::formatDate(timestamp);
  return formatted.empty() ? std::string(tr(STR_NOT_SET)) : formatted;
}

std::string formatDateRange(const uint32_t startTimestamp, const uint32_t endTimestamp) {
  const std::string start = TimeUtils::formatDate(startTimestamp);
  const std::string end = TimeUtils::formatDate(endTimestamp);
  if (start.empty() && end.empty()) return tr(STR_NOT_SET);
  const std::string endStr = end.empty() ? tr(STR_IN_PROGRESS) : end;
  return (start.empty() ? tr(STR_NOT_SET) : start) + " - " + endStr;
}

uint32_t getCompletionDateForDisplay(const ReadingBookStats& book) { return book.completedAt; }

uint64_t roundUpEstimateMs(const uint64_t valueMs) {
  if (valueMs == 0) {
    return 0;
  }
  return ((valueMs + ESTIMATE_ROUNDING_MS - 1) / ESTIMATE_ROUNDING_MS) * ESTIMATE_ROUNDING_MS;
}

std::string buildSessionEstimateText(const uint64_t remainingMs, const ReadingBookStats& book) {
  if (book.sessions == 0) {
    return "";
  }

  const uint64_t averageSessionMs = book.totalReadingMs / book.sessions;
  if (averageSessionMs < MIN_ESTIMATE_AVG_SESSION_MS) {
    return "";
  }

  const uint32_t sessionsLeft = static_cast<uint32_t>((remainingMs + averageSessionMs - 1) / averageSessionMs);
  if (sessionsLeft == 0) {
    return "";
  }

  return std::to_string(sessionsLeft) + " " +
         (sessionsLeft == 1 ? std::string(tr(STR_SESSION)) : std::string(tr(STR_SESSIONS)));
}

std::string buildEstimatedTimeLeftText(const ReadingBookStats& book) {
  if (book.completed || book.lastProgressPercent >= 100) {
    return tr(STR_DONE);
  }

  if (book.totalReadingMs < MIN_ESTIMATE_READING_MS || book.lastProgressPercent < MIN_ESTIMATE_PROGRESS_PERCENT) {
    return tr(STR_ESTIMATE_AFTER_MORE_READING);
  }

  const uint64_t estimatedTotalMs =
      (book.totalReadingMs * 100ULL + book.lastProgressPercent - 1) / book.lastProgressPercent;
  if (estimatedTotalMs <= book.totalReadingMs) {
    return tr(STR_ESTIMATE_AFTER_MORE_READING);
  }

  const uint64_t remainingMs = roundUpEstimateMs(estimatedTotalMs - book.totalReadingMs);
  std::string estimateText = "~" + ReadingStatsAnalytics::formatDurationHm(remainingMs);
  const std::string sessionText = buildSessionEstimateText(remainingMs, book);
  if (!sessionText.empty()) {
    estimateText += " / " + sessionText;
  }
  return estimateText;
}
void drawDetailCard(const GfxRenderer& renderer, const Rect& rect, const char* label,
                    const std::string& value) {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const int padX = 10;
  const int smallFont = SMALL_FONT_ID;
  const int labelH = renderer.getLineHeight(smallFont);

  // 1. Label on top
  renderer.drawText(smallFont, rect.x + padX, rect.y + 6, label);

  // 2. Value in middle/bottom (bold, shrinks if too wide)
  const int valFont = renderer.getTextWidth(UI_12_FONT_ID, value.c_str(), EpdFontFamily::BOLD) > (rect.width - padX * 2)
                          ? UI_10_FONT_ID
                          : UI_12_FONT_ID;
  const int valY = rect.y + 6 + labelH + 4;
  renderer.drawText(valFont, rect.x + padX, valY, value.c_str(), true, EpdFontFamily::BOLD);
}

void drawAdjustTimeButton(const GfxRenderer& renderer, const Rect& rect, const bool selected) {
  if (selected) {
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, true);
  } else {
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  }
  const char* label = tr(STR_ADJUST);
  const int font = UI_10_FONT_ID;
  const int textW = renderer.getTextWidth(font, label);
  const int textX = rect.x + (rect.width - textW) / 2;
  const int textY = rect.y + (rect.height - renderer.getLineHeight(font)) / 2;
  renderer.drawText(font, textX, textY, label, !selected, EpdFontFamily::BOLD);
}

Rect offsetRect(Rect rect, const int dy) {
  rect.y += dy;
  return rect;
}

struct DetailHeroLayout {
  Rect coverBase;
  Rect adjustBase;
  Rect cover;
  Rect adjust;
};

DetailHeroLayout detailHeroLayout(const ThemeMetrics& metrics, const int contentTop, const int coverWidth,
                                  const int coverHeight, const int scrollOffset) {
  const Rect coverBase{metrics.contentSidePadding, contentTop, coverWidth, coverHeight};
  const Rect adjustBase{coverBase.x, coverBase.y + coverBase.height + 6, coverBase.width, 28};
  return {coverBase, adjustBase, offsetRect(coverBase, -scrollOffset), offsetRect(adjustBase, -scrollOffset)};
}

void drawSummaryBanner(const GfxRenderer& renderer, const Rect& rect, const char* title, const std::string& summary,
                       const bool inverted = false) {
  const bool foregroundBlack = AppMetricCard::drawSelectablePanel(renderer, rect, inverted, true, true);

  renderer.drawText(UI_10_FONT_ID, rect.x + 10, rect.y + 6, title, foregroundBlack, EpdFontFamily::BOLD);
  const auto summaryLines =
      renderer.wrappedText(UI_10_FONT_ID, summary.c_str(), rect.width - 20, 2, EpdFontFamily::REGULAR);
  int summaryY = rect.y + 23;
  for (const auto& line : summaryLines) {
    renderer.drawText(UI_10_FONT_ID, rect.x + 10, summaryY, line.c_str(), foregroundBlack, EpdFontFamily::REGULAR);
    summaryY += renderer.getLineHeight(UI_10_FONT_ID);
  }
}

void drawProgressBlock(const GfxRenderer& renderer, const Rect& rect, const char* label, const uint8_t percent) {
  const std::string percentText = std::to_string(std::min<int>(percent, 100)) + "%";
  const int percentWidth = renderer.getTextWidth(UI_10_FONT_ID, percentText.c_str(), EpdFontFamily::BOLD);

  renderer.drawText(UI_10_FONT_ID, rect.x, rect.y, label);
  renderer.drawText(UI_10_FONT_ID, rect.x + rect.width - percentWidth, rect.y, percentText.c_str(), true,
                    EpdFontFamily::BOLD);

  const int barTop = rect.y + renderer.getLineHeight(UI_10_FONT_ID) + 4;
  const Rect barRect{rect.x, barTop, rect.width, 5};
  InxInkCards::drawHairProgress(renderer, barRect, percent);
}

void drawCover(const GfxRenderer& renderer, const Rect& rect, const std::string& coverPath, const ReadingBookStats* book) {
  const auto drawFallback = [&renderer, &rect, book]() {
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
    renderer.drawLine(rect.x + 5, rect.y, rect.x + 5, rect.y + rect.height - 1);

    if (book) {
      const char* title = book->title.empty() ? book->path.c_str() : book->title.c_str();
      const int titleW = rect.width - 12;
      const int tFont = renderer.getTextWidth(UI_10_FONT_ID, title, EpdFontFamily::BOLD) > titleW
                            ? SMALL_FONT_ID
                            : UI_10_FONT_ID;
      const auto lines = renderer.wrappedText(tFont, title, titleW, 2, EpdFontFamily::BOLD);
      int ty = rect.y + 20;
      for (const auto& l : lines) {
        const int lw = renderer.getTextWidth(tFont, l.c_str(), EpdFontFamily::BOLD);
        renderer.drawText(tFont, rect.x + 5 + (titleW - lw) / 2, ty, l.c_str(), true, EpdFontFamily::BOLD);
        ty += renderer.getLineHeight(tFont) + 3;
      }
      if (!book->author.empty()) {
        const int aw = renderer.getTextWidth(SMALL_FONT_ID, book->author.c_str());
        renderer.drawText(SMALL_FONT_ID, rect.x + 5 + (titleW - aw) / 2, rect.y + rect.height - 24, book->author.c_str());
      }
    } else {
      const char* label = tr(STR_BOOK);
      const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, rect.x + (rect.width - textWidth) / 2, rect.y + rect.height / 2, label, true, EpdFontFamily::BOLD);
    }
  };

  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  if (coverPath.empty()) {
    drawFallback();
    return;
  }

  HalFile file;
  if (!Storage.openFileForRead("RSD", coverPath, file)) {
    drawFallback();
    return;
  }

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() == BmpReaderError::Ok) {
    if (!renderer.drawBitmapCropToFill(bitmap, rect.x + 2, rect.y + 2, rect.width - 4, rect.height - 4)) {
      drawFallback();
    }
  } else {
    drawFallback();
  }
}
}  // namespace

void ReadingStatsDetailActivity::onEnter() {
  Activity::onEnter();
  invalidateBaseScreenBuffer();
  resolvedCoverBmpPath.clear();
  coverLoadPending = false;
  selectedStatsItem = 0;
  scrollOffset = 0;
  maxScrollOffset = 0;
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  waitForBackRelease = false;
  if (const auto* book = findBook(bookPath)) {
    resolvedCoverBmpPath = findFastCoverPath(*book);
    coverLoadPending = resolvedCoverBmpPath.empty();
  }
  requestUpdate();
}

void ReadingStatsDetailActivity::onExit() {
  Activity::onExit();
  freeBaseScreenBuffer();
}

bool ReadingStatsDetailActivity::storeBaseScreenBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  freeBaseScreenBuffer();

  const size_t bufferSize = renderer.getBufferSize();
  baseScreenBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!baseScreenBuffer) {
    return false;
  }

  memcpy(baseScreenBuffer, frameBuffer, bufferSize);
  baseScreenBufferStored = true;
  baseScreenBookPath = bookPath;
  baseScreenCoverPath = resolvedCoverBmpPath;
  baseScreenScrollOffset = scrollOffset;
  return true;
}

bool ReadingStatsDetailActivity::restoreBaseScreenBuffer() {
  if (!baseScreenBufferStored || !baseScreenBuffer || baseScreenBookPath != bookPath ||
      baseScreenCoverPath != resolvedCoverBmpPath || baseScreenScrollOffset != scrollOffset) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  memcpy(frameBuffer, baseScreenBuffer, renderer.getBufferSize());
  return true;
}

void ReadingStatsDetailActivity::invalidateBaseScreenBuffer() {
  baseScreenBufferStored = false;
  baseScreenBookPath.clear();
  baseScreenCoverPath.clear();
  baseScreenScrollOffset = -1;
}

void ReadingStatsDetailActivity::freeBaseScreenBuffer() {
  if (baseScreenBuffer) {
    free(baseScreenBuffer);
    baseScreenBuffer = nullptr;
  }
  invalidateBaseScreenBuffer();
}

void ReadingStatsDetailActivity::openAdjustment() {
  const auto* book = findBook(bookPath);
  if (book == nullptr) {
    requestUpdate();
    return;
  }

  startActivityForResultWith<BookReadingAdjustmentActivity>(
      [this](const ActivityResult&) {
        guardChildReturn();
        requestUpdate();
      },
      book->path, getDisplayTitle(*book));
}

void ReadingStatsDetailActivity::guardChildReturn() {
  invalidateBaseScreenBuffer();
  waitForBackRelease = true;
  waitForConfirmRelease = true;
}

void ReadingStatsDetailActivity::loop() {
  if (waitForBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      waitForBackRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (waitForConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      waitForConfirmRelease = false;
    }
    return;
  }

  const auto scrollBy = [&](const int delta) {
    const int nextOffset = std::clamp(scrollOffset + delta, 0, maxScrollOffset);
    if (nextOffset == scrollOffset) {
      return false;
    }
    scrollOffset = nextOffset;
    selectedStatsItem = 0;
    invalidateBaseScreenBuffer();
    requestUpdate();
    return true;
  };

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    scrollBy(DETAIL_SCROLL_STEP);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    scrollBy(-DETAIL_SCROLL_STEP);
    return;
  }

  buttonNavigator.onNextPress([&]() {
    if (maxScrollOffset > 0) {
      if (scrollOffset == 0 && selectedStatsItem == 0) {
        selectedStatsItem = DETAIL_ADJUST_FOCUS_INDEX;
        requestUpdate();
        return;
      }
      if (scrollOffset < maxScrollOffset && scrollBy(DETAIL_SCROLL_STEP)) {
        return;
      }
      return;
    }

    selectedStatsItem = ButtonNavigator::nextIndex(selectedStatsItem, DETAIL_FOCUS_ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPreviousPress([&]() {
    if (maxScrollOffset > 0) {
      if (scrollOffset > 0 && scrollBy(-DETAIL_SCROLL_STEP)) {
        return;
      }
      selectedStatsItem = ButtonNavigator::previousIndex(selectedStatsItem, DETAIL_FOCUS_ITEM_COUNT);
      requestUpdate();
      return;
    }

    selectedStatsItem = ButtonNavigator::previousIndex(selectedStatsItem, DETAIL_FOCUS_ITEM_COUNT);
    requestUpdate();
  });

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const auto coverSize = UITheme::getInstance().hasMainTabs() ? InxCoverGeometry::fit(COVER_WIDTH, COVER_HEIGHT)
                                                              : InxCoverGeometry::Size{COVER_WIDTH, COVER_HEIGHT};
  const DetailHeroLayout hero = detailHeroLayout(metrics, contentTop, coverSize.width, coverSize.height, scrollOffset);
  const auto contains = [](const Rect& rect, const int x, const int y) {
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
  };

  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTouchDown(touchX, touchY)) {
    const int touchedItem = contains(hero.adjust, touchX, touchY) ? DETAIL_ADJUST_FOCUS_INDEX : 0;
    if ((contains(hero.cover, touchX, touchY) || touchedItem == DETAIL_ADJUST_FOCUS_INDEX) &&
        selectedStatsItem != touchedItem) {
      selectedStatsItem = touchedItem;
      requestUpdate();
    }
  }
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    if (contains(hero.adjust, touchX, touchY)) {
      selectedStatsItem = DETAIL_ADJUST_FOCUS_INDEX;
      openAdjustment();
      return;
    }
    if (contains(hero.cover, touchX, touchY) && Storage.exists(bookPath.c_str())) {
      selectedStatsItem = 0;
      onSelectBook(bookPath);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && selectedStatsItem == DETAIL_ADJUST_FOCUS_INDEX) {
    openAdjustment();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && Storage.exists(bookPath.c_str())) {
    onSelectBook(bookPath);
  }
}

void ReadingStatsDetailActivity::render(RenderLock&&) {
  if (coverLoadPending) {
    coverLoadPending = false;
    if (const auto* pendingBook = findBook(bookPath)) {
      std::string resolvedCoverPath;
      if (FsHelpers::hasEpubExtension(pendingBook->path)) {
        GfxRenderer::FrameBufferLoan loan(renderer);
        resolvedCoverPath = ensureCoverPath(*pendingBook);
      } else {
        resolvedCoverPath = ensureCoverPath(*pendingBook);
      }
      if (!resolvedCoverPath.empty() && resolvedCoverPath != resolvedCoverBmpPath) {
        resolvedCoverBmpPath = std::move(resolvedCoverPath);
        invalidateBaseScreenBuffer();
      }
    }
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const auto* book = findBook(bookPath);
  const auto& lastSessionSnapshot = READING_STATS.getLastSessionSnapshot();
  const bool showCompletionBanner = context.showSessionSummary && lastSessionSnapshot.valid &&
                                    lastSessionSnapshot.path == bookPath && lastSessionSnapshot.completedThisSession;

  if (!book) {
    renderer.clearScreen();
    invalidateBaseScreenBuffer();
    HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_STATS));
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, metrics.topPadding + metrics.headerHeight + 30,
                      tr(STR_NO_READING_STATS));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
  unsigned activeDays = 0;
  for (const auto& d : book->readingDays) if (d.readingMs > 0) ++activeDays;

  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int viewportBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const auto coverSize = UITheme::getInstance().hasMainTabs() ? InxCoverGeometry::fit(COVER_WIDTH, COVER_HEIGHT)
                                                              : InxCoverGeometry::Size{COVER_WIDTH, COVER_HEIGHT};
  const DetailHeroLayout hero = detailHeroLayout(metrics, contentTop, coverSize.width, coverSize.height, scrollOffset);
  const Rect coverBaseRect = hero.coverBase;
  const Rect adjustButtonBaseRect = hero.adjustBase;
  const int textX = coverBaseRect.x + coverBaseRect.width + 16;
  const int textWidth = pageWidth - textX - metrics.contentSidePadding;

  int currentY = contentTop + 6;
  const int titleTop = currentY;
  const auto wrappedTitle =
      renderer.wrappedText(UI_12_FONT_ID, getDisplayTitle(*book).c_str(), textWidth, 2, EpdFontFamily::BOLD);
  currentY += static_cast<int>(wrappedTitle.size()) * renderer.getLineHeight(UI_12_FONT_ID);

  const int authorTop = currentY + 4;
  if (!book->author.empty()) {
    currentY += renderer.getLineHeight(UI_10_FONT_ID) + 10;
  } else {
    currentY += 10;
  }

  currentY += 6;
  const int bookProgressTop = currentY;
  currentY += PROGRESS_BLOCK_HEIGHT + 14;
  const int chapterProgressTop = currentY;
  currentY += PROGRESS_BLOCK_HEIGHT + 14;
  const int chapterLabelTop = currentY;
  currentY += renderer.getLineHeight(UI_10_FONT_ID) + 6;
  const int chapterTextTop = currentY;
  const std::string currentChapter = book->chapterTitle.empty() ? std::string(tr(STR_NOT_SET)) : book->chapterTitle;
  const auto chapterLines =
      renderer.wrappedText(UI_10_FONT_ID, currentChapter.c_str(), textWidth, 2, EpdFontFamily::BOLD);
  currentY += static_cast<int>(chapterLines.size()) * renderer.getLineHeight(UI_10_FONT_ID);

  int cardsTop =
      std::max(adjustButtonBaseRect.y + adjustButtonBaseRect.height, currentY) + metrics.verticalSpacing + 10;
  const int summaryBannerTop = cardsTop;
  if (showCompletionBanner) {
    cardsTop += SUMMARY_BANNER_HEIGHT + SUMMARY_BANNER_GAP;
  }

  const int estimateCardTop = cardsTop + (METRIC_CARD_HEIGHT + METRIC_CARD_GAP) * 4;
  const int contentBottom = estimateCardTop + METRIC_CARD_HEIGHT + metrics.verticalSpacing;
  maxScrollOffset = std::max(0, contentBottom - viewportBottom);
  scrollOffset = std::clamp(scrollOffset, 0, maxScrollOffset);
  const int scrollDy = -scrollOffset;
  const Rect coverRect = offsetRect(coverBaseRect, scrollDy);
  const Rect adjustButtonRect = offsetRect(adjustButtonBaseRect, scrollDy);
  const bool adjustSelected = scrollOffset == 0 && selectedStatsItem == DETAIL_ADJUST_FOCUS_INDEX;

  const bool baseScreenRestored = restoreBaseScreenBuffer();
  if (!baseScreenRestored) {
    renderer.clearScreen();
    {
      // Clip the scrolling detail content (cover, title, progress blocks, metric
      // cards) to the viewport so elements scrolled past the screen edges are
      // dropped silently instead of logging "Outside range". Masks/header below
      // and the base-screen store stay outside this scope (clip auto-cleared).
      const GfxRenderer::ClipScope clip(renderer, 0, contentTop, pageWidth, viewportBottom - contentTop);

      drawCover(renderer, coverRect, resolvedCoverBmpPath, book);
      drawAdjustTimeButton(renderer, adjustButtonRect, false);

      currentY = titleTop + scrollDy;
      for (const auto& line : wrappedTitle) {
        renderer.drawText(UI_12_FONT_ID, textX, currentY, line.c_str(), true, EpdFontFamily::BOLD);
        currentY += renderer.getLineHeight(UI_12_FONT_ID);
      }

      if (!book->author.empty()) {
        renderer.drawText(UI_10_FONT_ID, textX, authorTop + scrollDy, book->author.c_str());
      }

      drawProgressBlock(renderer, Rect{textX, bookProgressTop + scrollDy, textWidth, PROGRESS_BLOCK_HEIGHT},
                        tr(STR_BOOK_PROGRESS), book->lastProgressPercent);
      drawProgressBlock(renderer, Rect{textX, chapterProgressTop + scrollDy, textWidth, PROGRESS_BLOCK_HEIGHT},
                        tr(STR_CHAPTER_PROGRESS), book->chapterProgressPercent);

      renderer.drawText(UI_10_FONT_ID, textX, chapterLabelTop + scrollDy, tr(STR_CURRENT_CHAPTER));

      currentY = chapterTextTop + scrollDy;
      for (const auto& line : chapterLines) {
        renderer.drawText(UI_10_FONT_ID, textX, currentY, line.c_str(), true, EpdFontFamily::BOLD);
        currentY += renderer.getLineHeight(UI_10_FONT_ID);
      }

      int drawCardsTop = cardsTop + scrollDy;
      if (showCompletionBanner) {
        drawSummaryBanner(renderer,
                          Rect{metrics.contentSidePadding, summaryBannerTop + scrollDy,
                               pageWidth - metrics.contentSidePadding * 2, SUMMARY_BANNER_HEIGHT},
                          tr(STR_BOOK_FINISHED), tr(STR_COMPLETED_THIS_SESSION), true);
      }
      const int cardWidth = (pageWidth - metrics.contentSidePadding * 2 - METRIC_CARD_GAP) / 2;
      constexpr int kCardH = 70;

      // Card 1: 上次翻阅
      std::string lastReadVal = book->lastSessionMs > 0 ? ReadingStatsAnalytics::formatDurationHm(book->lastSessionMs) : "暂无";
      drawDetailCard(renderer, Rect{metrics.contentSidePadding, drawCardsTop, cardWidth, kCardH},
                     "上次翻阅", lastReadVal);

      // Card 2: 累计总阅读用时
      drawDetailCard(renderer,
                     Rect{metrics.contentSidePadding + cardWidth + METRIC_CARD_GAP, drawCardsTop, cardWidth, kCardH},
                     "累计总阅读用时", ReadingStatsAnalytics::formatDurationHm(book->totalReadingMs));

      // Card 3: 翻开阅读频次
      drawDetailCard(renderer,
                     Rect{metrics.contentSidePadding, drawCardsTop + kCardH + METRIC_CARD_GAP, cardWidth, kCardH},
                     "翻开阅读频次", std::to_string(book->sessions) + " 次");

      // Card 4: 当前阅读状态
      drawDetailCard(renderer,
                     Rect{metrics.contentSidePadding + cardWidth + METRIC_CARD_GAP,
                          drawCardsTop + kCardH + METRIC_CARD_GAP, cardWidth, kCardH},
                     "当前阅读状态", book->completed ? "已读完" : "在读中");

      // Card 5 (full width): 阅读周期记录
      drawDetailCard(renderer,
                     Rect{metrics.contentSidePadding, drawCardsTop + (kCardH + METRIC_CARD_GAP) * 2,
                          pageWidth - metrics.contentSidePadding * 2, kCardH},
                     "阅读周期记录", formatDateRange(book->firstReadAt, getCompletionDateForDisplay(*book)));

      // Card 6 (full width): 预计剩余时间
      drawDetailCard(renderer,
                     Rect{metrics.contentSidePadding, drawCardsTop + (kCardH + METRIC_CARD_GAP) * 3,
                          pageWidth - metrics.contentSidePadding * 2, kCardH},
                     "预计剩余时间", buildEstimatedTimeLeftText(*book));

      // Bottom action button: 继续阅读本书
      const int btnY = drawCardsTop + (kCardH + METRIC_CARD_GAP) * 4 + 6;
      GUI.drawPaperAction(renderer,
                          Rect{metrics.contentSidePadding, btnY, pageWidth - metrics.contentSidePadding * 2, 42},
                          "继续阅读本书  ›", false, true);
    }

    renderer.fillRect(0, 0, pageWidth, contentTop, false);
    if (viewportBottom < pageHeight) {
      renderer.fillRect(0, viewportBottom, pageWidth, pageHeight - viewportBottom, false);
    }
    if (GUI.usesPaperStyle()) {
      const int hY = metrics.topPadding + 6;
      GUI.drawPaperText(renderer, Rect{metrics.contentSidePadding, hY, 80, 24}, UI_10_FONT_ID, "‹ 返回");
      const int titleW = renderer.getTextWidth(UI_12_FONT_ID, "书籍阅读统计", EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, (pageWidth - titleW) / 2, hY, "书籍阅读统计", true, EpdFontFamily::BOLD);
      char rBuf[32] = {};
      snprintf(rBuf, sizeof(rBuf), "已读 %u 天", activeDays);
      const int rW = renderer.getTextWidth(UI_10_FONT_ID, rBuf);
      renderer.drawText(UI_10_FONT_ID, pageWidth - metrics.contentSidePadding - rW, hY, rBuf);
      GUI.drawPaperRule(renderer, Rect{metrics.contentSidePadding, contentTop - 2, pageWidth - metrics.contentSidePadding * 2, 1});
    } else {
      HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_STATS));
    }

    storeBaseScreenBuffer();
  }

  if (adjustSelected) {
    drawAdjustTimeButton(renderer, adjustButtonRect, true);
  }

  const char* confirmLabel = adjustSelected ? tr(STR_ADJUST) : (Storage.exists(bookPath.c_str()) ? tr(STR_OPEN) : "");
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
