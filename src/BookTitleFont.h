#pragma once

#include <CrossPointSettings.h>
#include <SdCardFontSystem.h>

#include "fontIds.h"

// Font for book titles on home/stats cards. Titles must render every common
// character: without an SD family the built-in 14/16/18pt CJK headers only
// carry UI-string glyphs (~750 chars), so a reader size above 12pt would show
// tofu in book titles. Fall back to UI_12, whose header carries the full
// common-character subset (3517 chars), until an SD font covers the reader
// size.
inline int bookTitleFontId() {
  const int readerFont = SETTINGS.getReaderFontId();
  if (readerFont == 0) return UI_12_FONT_ID;
  if (SETTINGS.fontPointSize <= 12) return readerFont;  // built-in 12pt: full subset
  if (SETTINGS.sdFontFamilyName[0] != '\0' &&
      sdFontSystem.resolveFontId(SETTINGS.sdFontFamilyName, SETTINGS.fontPointSize) != 0) {
    return readerFont;  // SD family active at this size: complete coverage
  }
  return UI_12_FONT_ID;
}
