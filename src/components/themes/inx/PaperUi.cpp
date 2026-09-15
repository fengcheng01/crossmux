#include <GfxRenderer.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "I18n.h"
#include "components/UITheme.h"
#include "components/icons/inx_apps.h"
#include "components/themes/BaseTheme.h"
#include "util/TimeUtils.h"

const BaseTheme::PaperMetrics& BaseTheme::paperMetrics() const {
  static constexpr PaperMetrics metrics{};
  return metrics;
}

BaseTheme::PaperHomeLayout BaseTheme::paperHomeLayout(const GfxRenderer& renderer, const Rect content) const {
  const auto& m = paperMetrics();
  PaperHomeLayout layout;
  layout.status = Rect{content.x, content.y, content.width, m.statusHeight};
  const int width = std::max(1, content.width - m.padding * 2);
  const int bodyHeight = renderer.getLineHeight(m.bodyFont);
  const int smallHeight = renderer.getLineHeight(m.smallFont);
  layout.kicker = Rect{content.x + m.padding, content.y + m.statusHeight, width, bodyHeight + 12};
  const int top = layout.kicker.y + layout.kicker.height + 16;
  const int bottom = content.y + content.height - 12;
  const bool wide = content.width > content.height;
  const int heroWidth = wide ? (width - m.gap) / 2 : width;
  const int heroHeight =
      wide ? bottom - top - smallHeight - 27 : std::min(232, std::max(180, content.height * 32 / 100));
  layout.hero = Rect{layout.kicker.x, top, heroWidth, heroHeight};
  const int coverWidth = std::min(wide ? 112 : 144, heroWidth * 2 / 5);
  const int coverHeight = std::min(heroHeight, coverWidth * 250 / 170);
  layout.cover = Rect{layout.hero.x, top, coverWidth, coverHeight};
  layout.details = Rect{layout.cover.x + coverWidth + m.gap, top, heroWidth - coverWidth - m.gap, heroHeight};
  const int noteHeight = renderer.getLineHeight(m.bookFont) + 12;
  layout.note = Rect{layout.hero.x, top + heroHeight + 8, wide ? heroWidth : width, noteHeight};
  const int shelfX = wide ? layout.hero.x + heroWidth + m.gap : layout.hero.x;
  const int shelfTop = wide ? top : layout.note.y + layout.note.height + 16;
  layout.shelfHeading = Rect{shelfX, shelfTop, wide ? width - heroWidth - m.gap : width, std::max(44, bodyHeight + 8)};
  const int shelfY = shelfTop + layout.shelfHeading.height;
  layout.footer = Rect{shelfX, bottom, layout.shelfHeading.width, 0};
  layout.shelf = Rect{shelfX, shelfY, layout.shelfHeading.width, std::max(0, bottom - 8 - shelfY)};
  const int rowMinimum = renderer.getLineHeight(m.bookFont) + smallHeight + 16;
  layout.shelfCapacity = std::max(1, std::min(3, layout.shelf.height / rowMinimum));
  return layout;
}

void BaseTheme::drawPaperStatus(const GfxRenderer& renderer, const Rect rect) const {
  if (rect.width <= 0 || rect.height <= 0) return;
  const auto& metrics = paperMetrics();
  const GfxRenderer::ClipScope clip(renderer, rect.x, rect.y, rect.width, rect.height);
  char time[16] = {};
  TimeUtils::formatCurrentTime(time, sizeof(time), SETTINGS.clockFormat == 1);
  renderer.drawText(metrics.smallFont, rect.x + metrics.padding, rect.y + 6, time);
  const auto& theme = UITheme::getInstance().getMetrics();
  drawBatteryRight(renderer,
                   Rect{rect.x + rect.width - metrics.padding - theme.batteryWidth, rect.y + 6, theme.batteryWidth,
                        theme.batteryHeight},
                   SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS);
}

BaseTheme::PaperAppLayout BaseTheme::paperAppLayout(const GfxRenderer& renderer, const Rect content) const {
  const auto& m = paperMetrics();
  const int bodyHeight = renderer.getLineHeight(m.bodyFont);
  const bool wide = content.width > content.height;
  PaperAppLayout layout;
  layout.columns = wide ? 4 : 3;
  layout.rows = wide ? 2 : 3;
  const int footerHeight = std::max(48, renderer.getLineHeight(m.smallFont) + 16);
  const int footerY = content.y + content.height - footerHeight - 8;
  layout.footer = Rect{content.x + m.padding, footerY, content.width - m.padding * 2, footerHeight};
  layout.previous = Rect{layout.footer.x + layout.footer.width - 136, footerY, 44, footerHeight};
  layout.next = Rect{layout.footer.x + layout.footer.width - 44, footerY, 44, footerHeight};
  const int top = content.y + m.statusHeight + m.headingHeight + bodyHeight + (wide ? 20 : 32);
  layout.grid = Rect{content.x + m.padding, top, content.width - m.padding * 2, std::max(0, footerY - top - 12)};
  return layout;
}

void BaseTheme::drawPaperBookmark(const GfxRenderer& renderer, const Rect rect) const {
  if (rect.width <= 0 || rect.height <= 0) return;
  for (int x = 0; x < rect.width; ++x) {
    const int cut = std::min(x, rect.width - 1 - x) * std::max(1, rect.height / 3) / std::max(1, rect.width / 2);
    renderer.fillRect(rect.x + x, rect.y, 1, rect.height - cut, true);
  }
}

void BaseTheme::drawPaperAppIcon(const GfxRenderer& renderer, const Rect rect, const UIIcon icon) const {
  const GfxRenderer::ClipScope clip(renderer, rect.x, rect.y, rect.width, rect.height);
  InxAppIcons::draw(renderer, icon, rect.x + (rect.width - InxAppIcons::size) / 2,
                    rect.y + (rect.height - InxAppIcons::size) / 2, 1, false);
}

int BaseTheme::drawPaperText(const GfxRenderer& renderer, const Rect rect, const int font, const char* text,
                             const bool bold, const int maxLines) const {
  if (!text || !*text || rect.width <= 0 || rect.height <= 0) return rect.y;
  const int lineHeight = renderer.getLineHeight(font);
  if (lineHeight <= 0 || rect.height < lineHeight) return rect.y;
  const int count = std::min({maxLines, 3, rect.height / lineHeight});
  if (count <= 0) return rect.y;
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const GfxRenderer::ClipScope clip(renderer, rect.x, rect.y, rect.width, rect.height);
  if (renderer.getTextWidth(font, text, style) <= rect.width) {
    renderer.drawText(font, rect.x, rect.y, text, true, style);
    return rect.y + lineHeight;
  }
  // Cap the renderer's temporary wrapping strings as well as the line count.
  // Complete UTF-8 characters and an ellipsis fit in this 192-byte stack buffer.
  char bounded[192] = {};
  size_t length = std::strlen(text);
  if (length >= sizeof(bounded)) {
    length = sizeof(bounded) - 4;
    while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80) --length;
    std::memcpy(bounded, text, length);
    std::memcpy(bounded + length, "\xe2\x80\xa6", 3);
  } else {
    std::memcpy(bounded, text, length);
  }
  if (count == 1) {
    const auto shown = renderer.truncatedText(font, bounded, rect.width, style);
    renderer.drawText(font, rect.x, rect.y, shown.c_str(), true, style);
    return rect.y + lineHeight;
  }
  const auto lines = renderer.wrappedText(font, bounded, rect.width, count, style);
  int y = rect.y;
  for (const auto& line : lines) {
    renderer.drawText(font, rect.x, y, line.c_str(), true, style);
    y += lineHeight;
  }
  return y;
}

void BaseTheme::drawPaperRule(const GfxRenderer& renderer, const Rect rect, const int thickness) const {
  if (rect.width > 0 && rect.height > 0) {
    renderer.fillRect(rect.x, rect.y, rect.width, std::min(rect.height, thickness), true);
  }
}

int BaseTheme::drawPaperHeading(const GfxRenderer& renderer, const Rect rect, const char* title,
                                const char* detail) const {
  const auto& metrics = paperMetrics();
  const int titleFont = rect.height < metrics.headingHeight ? metrics.bodyFont : metrics.titleFont;
  const int titleHeight = renderer.getLineHeight(titleFont);
  int detailWidth = detail && *detail ? renderer.getTextWidth(metrics.smallFont, detail) : 0;
  detailWidth = std::min(detailWidth, rect.width / 2);
  const int titleWidth = rect.width - (detailWidth ? detailWidth + metrics.gap : 0);
  drawPaperText(renderer, Rect{rect.x, rect.y + 4, titleWidth, titleHeight}, titleFont, title, true);
  if (detailWidth > 0) {
    const int h = renderer.getLineHeight(metrics.smallFont);
    drawPaperText(renderer, Rect{rect.x + rect.width - detailWidth, rect.y + 4 + titleHeight - h, detailWidth, h},
                  metrics.smallFont, detail);
  }
  drawPaperRule(renderer, Rect{rect.x, rect.y + rect.height - 2, rect.width, 2}, 2);
  return rect.y + rect.height;
}

void BaseTheme::drawPaperFocus(const GfxRenderer& renderer, const Rect rect) const {
  if (rect.width > 0 && rect.height > 0) renderer.fillRect(rect.x, rect.y + 4, 3, std::max(1, rect.height - 8), true);
}

void BaseTheme::drawPaperProgress(const GfxRenderer& renderer, const Rect rect, const uint8_t percent) const {
  if (rect.width <= 0 || rect.height <= 0) return;
  for (int x = rect.x; x < rect.x + rect.width; x += 3) renderer.drawPixel(x, rect.y, true);
  const int fill = rect.width * std::min<int>(percent, 100) / 100;
  if (fill > 0) renderer.fillRect(rect.x, rect.y, fill, rect.height, true);
}

void BaseTheme::drawPaperCover(const GfxRenderer& renderer, const Rect rect, const char* title,
                               const char* author) const {
  if (rect.width <= 0 || rect.height <= 0) return;
  const auto& metrics = paperMetrics();
  const GfxRenderer::ClipScope clip(renderer, rect.x, rect.y, rect.width, rect.height);
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, 1, true);
  renderer.drawLine(rect.x + 6, rect.y, rect.x + 6, rect.y + rect.height - 1, true);
  const int pad = std::min(16, rect.width / 8);
  const int authorHeight = renderer.getLineHeight(metrics.smallFont);
  const int lineHeight = renderer.getLineHeight(metrics.smallFont);
  const int titleLines = std::min(7, (rect.height - authorHeight - pad * 3) / std::max(1, lineHeight));
  const char* cursor = title ? title : "";
  // A glyph-sized stack buffer keeps the fallback cover quiet without allocating a second title.
  if (*cursor && static_cast<unsigned char>(*cursor) < 0x80) {
    drawPaperText(renderer, Rect{rect.x + pad, rect.y + rect.height / 3, rect.width - pad * 2, lineHeight * 3},
                  metrics.smallFont, cursor, false, 3);
  } else
    for (int i = 0; i < titleLines && *cursor; ++i) {
      char glyph[5] = {};
      const auto first = static_cast<unsigned char>(*cursor);
      int bytes = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
      for (int j = 1; j < bytes; ++j) {
        if (!cursor[j] || (static_cast<unsigned char>(cursor[j]) & 0xc0) != 0x80) {
          bytes = 1;
          break;
        }
      }
      if (i == titleLines - 1 && cursor[bytes])
        std::memcpy(glyph, "\xe2\x80\xa6", 3);
      else
        std::memcpy(glyph, cursor, bytes);
      const int width = renderer.getTextWidth(metrics.smallFont, glyph);
      drawPaperText(renderer, Rect{rect.x + (rect.width - width) / 2, rect.y + pad + i * lineHeight, width, lineHeight},
                    metrics.smallFont, glyph);
      cursor += bytes;
    }
  drawPaperText(renderer,
                Rect{rect.x + pad, rect.y + rect.height - authorHeight - pad, rect.width - pad * 2, authorHeight},
                metrics.smallFont, author);
}

void BaseTheme::drawPaperAction(const GfxRenderer& renderer, const Rect rect, const char* label, const bool selected,
                                const bool filled) const {
  const auto& metrics = paperMetrics();
  const int h = renderer.getLineHeight(metrics.bodyFont);
  if (filled) {
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, true);
    const auto shown =
        renderer.truncatedText(metrics.bodyFont, label, std::max(1, rect.width - 44), EpdFontFamily::BOLD);
    renderer.drawText(metrics.bodyFont, rect.x + 12, rect.y + std::max(0, (rect.height - h) / 2), shown.c_str(), false,
                      EpdFontFamily::BOLD);
    renderer.drawText(metrics.bodyFont, rect.x + rect.width - 28, rect.y + std::max(0, (rect.height - h) / 2), ">",
                      false);
    if (selected) renderer.drawRect(rect.x + 3, rect.y + 3, rect.width - 6, rect.height - 6, 1, false);
    return;
  }
  drawPaperText(renderer, Rect{rect.x + 8, rect.y + std::max(0, (rect.height - h) / 2), rect.width - 32, h},
                metrics.bodyFont, label, true);
  drawPaperText(renderer, Rect{rect.x + rect.width - 24, rect.y + std::max(0, (rect.height - h) / 2), 24, h},
                metrics.bodyFont, ">");
  drawPaperRule(renderer, Rect{rect.x, rect.y + rect.height - 2, rect.width, 2}, 2);
  if (selected) drawPaperFocus(renderer, rect);
}

void BaseTheme::drawPaperBar(const GfxRenderer& renderer, const Rect rect, const bool emphasized) const {
  if (rect.width <= 0 || rect.height <= 0) return;
  if (emphasized) {
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, true);
    return;
  }
  const GfxRenderer::ClipScope clip(renderer, rect.x, rect.y, rect.width, rect.height);
  for (int offset = -rect.height; offset < rect.width; offset += 4) {
    renderer.drawLine(rect.x + offset, rect.y + rect.height - 1, rect.x + offset + rect.height, rect.y, true);
  }
}
