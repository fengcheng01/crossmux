#pragma once

#include <GfxRenderer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "fontIds.h"

// Shared INX home/stats chrome: rounded light-gray cards, large values, hairline
// progress. Keep drawing here so 阅读记录 and 阅读统计 stay on one design.
namespace InxInkCards {
constexpr int kRadius = 14;
constexpr int kGap = 12;
constexpr int kPagePad = 20;
constexpr int kProgressHeight = 4;

inline void drawCard(const GfxRenderer& renderer, const Rect rect) {
  if (rect.width <= 0 || rect.height <= 0) return;
  renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, kRadius, Color::LightGray);
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, kRadius, true);
}

inline void drawMetricCard(const GfxRenderer& renderer, const Rect rect, const char* value, const char* label) {
  drawCard(renderer, rect);
  const int pad = 14;
  const int innerW = std::max(1, rect.width - pad * 2);
  const int valueFont = NOTOSANS_18_FONT_ID;
  const int labelFont = SMALL_FONT_ID;
  const int valueH = renderer.getLineHeight(valueFont);
  const int labelH = renderer.getLineHeight(labelFont);
  const int blockH = valueH + 6 + labelH;
  int y = rect.y + std::max(pad, (rect.height - blockH) / 2);
  const std::string shownValue = renderer.truncatedText(valueFont, value, innerW, EpdFontFamily::BOLD);
  renderer.drawText(valueFont, rect.x + pad, y, shownValue.c_str(), true, EpdFontFamily::BOLD);
  y += valueH + 6;
  const std::string shownLabel = renderer.truncatedText(labelFont, label, innerW);
  renderer.drawText(labelFont, rect.x + pad, y, shownLabel.c_str());
}

inline void drawProgress(const GfxRenderer& renderer, const Rect rect, const uint8_t percent) {
  if (rect.width <= 0 || rect.height <= 0) return;
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  const int innerW = std::max(0, rect.width - 2);
  const int fill = innerW * std::min<int>(percent, 100) / 100;
  if (fill > 0) renderer.fillRect(rect.x + 1, rect.y + 1, fill, std::max(0, rect.height - 2));
}

inline void drawPageTitle(const GfxRenderer& renderer, const int x, const int y, const char* title) {
  renderer.drawText(NOTOSERIF_14_FONT_ID, x, y, title, true, EpdFontFamily::BOLD);
}

inline int pageTitleHeight(const GfxRenderer& renderer) { return renderer.getLineHeight(NOTOSERIF_14_FONT_ID); }

inline void drawDonut(const GfxRenderer& renderer, const int cx, const int cy, const int radius, const uint8_t percent,
                      const char* caption) {
  constexpr float kPi = 3.14159265358979323846f;
  const int thickness = std::max(3, radius / 12);
  const int inner = std::max(8, radius - thickness);
  for (int degree = 0; degree < 360; degree += 4) {
    const float angle = (static_cast<float>(degree) - 90.0f) * kPi / 180.0f;
    renderer.drawPixel(cx + static_cast<int>(std::cos(angle) * radius),
                       cy + static_cast<int>(std::sin(angle) * radius));
    renderer.drawPixel(cx + static_cast<int>(std::cos(angle) * inner), cy + static_cast<int>(std::sin(angle) * inner));
  }
  const int progressDegrees = std::min<int>(percent, 100) * 360 / 100;
  for (int degree = 0; degree < progressDegrees; ++degree) {
    const float angle = (static_cast<float>(degree) - 90.0f) * kPi / 180.0f;
    renderer.drawLine(cx + static_cast<int>(std::cos(angle) * inner), cy + static_cast<int>(std::sin(angle) * inner),
                      cx + static_cast<int>(std::cos(angle) * radius), cy + static_cast<int>(std::sin(angle) * radius),
                      2, true);
  }
  char label[8];
  snprintf(label, sizeof(label), "%u%%", static_cast<unsigned>(std::min<int>(percent, 100)));
  const int valueH = renderer.getLineHeight(NOTOSANS_18_FONT_ID);
  const int valueW = renderer.getTextWidth(NOTOSANS_18_FONT_ID, label, EpdFontFamily::BOLD);
  renderer.drawText(NOTOSANS_18_FONT_ID, cx - valueW / 2, cy - valueH, label, true, EpdFontFamily::BOLD);
  if (caption) {
    const int capW = renderer.getTextWidth(SMALL_FONT_ID, caption);
    renderer.drawText(SMALL_FONT_ID, cx - capW / 2, cy + 4, caption);
  }
}

inline void layoutGrid(const Rect bounds, const int columns, const int rows, const int index, Rect& out) {
  const int col = index % columns;
  const int row = index / columns;
  const int cellW = (bounds.width - kGap * (columns - 1)) / columns;
  const int cellH = (bounds.height - kGap * (rows - 1)) / rows;
  out = Rect{bounds.x + col * (cellW + kGap), bounds.y + row * (cellH + kGap), cellW, cellH};
}
}  // namespace InxInkCards
