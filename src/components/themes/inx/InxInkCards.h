#pragma once

#include <GfxRenderer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "fontIds.h"

// Shared INX home/stats chrome: white rounded cards, large values, hairline
// progress. Keep drawing here so 阅读记录 and 阅读统计 stay on one design.
// Do not fill these cards LightGray: 1-in-4 dither over a full panel reads as a
// washed sheet on M4 FAST and ghosts the previous page through the speckle.
namespace InxInkCards {
constexpr int kRadius = 14;
constexpr int kGap = 12;
constexpr int kPagePad = 20;
constexpr int kProgressHeight = 4;

inline void drawCard(const GfxRenderer& renderer, const Rect rect) {
  if (rect.width <= 0 || rect.height <= 0) return;
  renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, kRadius, Color::White);
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, kRadius, true);
}

// Minimum cell height drawMetricCard needs: value + gap + label line heights
// plus one top pad (the card anchors the block at max(pad, centered)). Layouts
// sizing a metric-card grid must give every row at least this much, or the
// label lands below the card and gets clipped (CN fonts are 3x instanced:
// UI_12 line = 36 px, SMALL line = 24 px).
inline int metricCardMinHeight(const GfxRenderer& renderer) {
  return renderer.getLineHeight(UI_12_FONT_ID) + 6 + renderer.getLineHeight(SMALL_FONT_ID) + 14;
}

inline void drawMetricCard(const GfxRenderer& renderer, const Rect rect, const char* value, const char* label) {
  drawCard(renderer, rect);
  const int pad = 14;
  const int innerW = std::max(1, rect.width - pad * 2);
  // UI_12 bold matches the list-layout AppMetricCard value size; the 18pt
  // slot's 54 px line needs cells so tall that 2x2 grids cannot fit it.
  const int valueFont = UI_12_FONT_ID;
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

inline void drawHairProgress(const GfxRenderer& renderer, const Rect rect, const uint8_t percent) {
  drawProgress(renderer, rect, percent);
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
  const int innerDiam = inner * 2;
  const int capH = caption ? renderer.getLineHeight(SMALL_FONT_ID) : 0;
  const int capGap = caption ? 2 : 0;
  int valueFont = SMALL_FONT_ID;
  // Prefer the largest face that still sits inside the hole. CN 18pt is 54 px
  // tall and "100%" overflows the ring (and the cards above it).
  const int candidates[] = {NOTOSANS_18_FONT_ID, UI_12_FONT_ID, SMALL_FONT_ID};
  for (const int id : candidates) {
    const int valueH = renderer.getLineHeight(id);
    const int valueW = renderer.getTextWidth(id, label, EpdFontFamily::BOLD);
    if (valueW <= innerDiam - 8 && valueH + capGap + capH <= innerDiam - 4) {
      valueFont = id;
      break;
    }
  }
  const int valueH = renderer.getLineHeight(valueFont);
  const int valueW = renderer.getTextWidth(valueFont, label, EpdFontFamily::BOLD);
  const int blockH = valueH + capGap + capH;
  const int textY = cy - blockH / 2;
  renderer.drawText(valueFont, cx - valueW / 2, textY, label, true, EpdFontFamily::BOLD);
  if (caption) {
    const int capW = renderer.getTextWidth(SMALL_FONT_ID, caption);
    renderer.drawText(SMALL_FONT_ID, cx - capW / 2, textY + valueH + capGap, caption);
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
