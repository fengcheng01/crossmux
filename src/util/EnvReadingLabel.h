#pragma once

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

// Builtin CJK does not include U+2103 (℃). Draw a degree ring + C so the unit
// stays visible on the M4 lock/standby screens.
inline void drawCelsiusHumidity(const GfxRenderer& renderer, const int fontId, const int y, const float tempC,
                                const float humidityPct, const int originX = 0, int centerWidth = 0) {
  char tempBuf[8];
  char humBuf[8];
  snprintf(tempBuf, sizeof(tempBuf), "%d", static_cast<int>(tempC + (tempC >= 0 ? 0.5f : -0.5f)));
  snprintf(humBuf, sizeof(humBuf), "%d%%", static_cast<int>(humidityPct + 0.5f));

  const int tempW = renderer.getTextWidth(fontId, tempBuf);
  const int cW = renderer.getTextWidth(fontId, "C");
  const int humW = renderer.getTextWidth(fontId, humBuf);
  const int lineH = renderer.getLineHeight(fontId);
  const int ringD = std::max(6, lineH / 5);
  const int ringGap = std::max(2, lineH / 12);
  const int unitGap = std::max(2, lineH / 10);
  const int groupGap = std::max(10, lineH / 2);
  const int totalW = tempW + unitGap + ringD + ringGap + cW + groupGap + humW;
  if (centerWidth <= 0) centerWidth = renderer.getScreenWidth();
  int x = originX + (centerWidth - totalW) / 2;

  renderer.drawText(fontId, x, y, tempBuf);
  x += tempW + unitGap;
  const int ringY = y + std::max(0, lineH / 8);
  renderer.drawRoundedRect(x, ringY, ringD, ringD, 1, ringD / 2, true);
  x += ringD + ringGap;
  renderer.drawText(fontId, x, y, "C");
  x += cW + groupGap;
  renderer.drawText(fontId, x, y, humBuf);
}
