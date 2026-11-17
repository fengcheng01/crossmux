#include <cassert>
#include <cstdio>
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>

int main() {
  GfxRenderer renderer;
  SdCardFontManager manager;
  const SdCardFontFamilyInfo reader{"ReaderKai", {{"reader12", 12}, {"reader14", 14}}};
  const SdCardFontFamilyInfo ui{"NotoSansSC", {{"ui12", 12}, {"ui14", 14}}};
  assert(manager.loadFamily(reader, renderer, 12));
  const int readerId = manager.getFontId(reader.name);
  const int uiId = manager.loadFamilyExtraSize(ui, renderer, 12);
  assert(readerId && uiId && readerId != uiId);
  assert(renderer.sd.at(readerId)->path == "reader12");
  assert(renderer.sd.at(uiId)->path == "ui12");
  assert(renderer.sd.at(uiId)->psramMetadata && !renderer.sd.at(readerId)->psramMetadata);
  assert(manager.loadFamilyExtraSize(ui, renderer, 12) == uiId);
  assert(SdCardFont::live == 2);
  assert(manager.loadFamilyExtraSize(ui, renderer, 10) == 0);

  assert(manager.loadFamily(reader, renderer, 14));
  assert(SdCardFont::live == 1 && renderer.fonts.size() == 1);
  const int nextUiId = manager.loadFamilyExtraSize(ui, renderer, 14);
  assert(nextUiId != manager.getFontId(reader.name));
  assert(renderer.sd.at(nextUiId)->path == "ui14");

  assert(manager.loadFamily(ui, renderer, 12));
  const int shared = manager.getFontId(ui.name);
  assert(manager.loadFamilyExtraSize(ui, renderer, 12) == shared);
  assert(SdCardFont::live == 1);  // Reader and UI can share the same Noto face.
  manager.unloadAll(renderer);
  assert(renderer.fonts.empty() && renderer.sd.empty() && SdCardFont::live == 0);

  assert(manager.loadFamilyExtraSize(ui, renderer, 12));
  assert(manager.getFontId("") == 0);  // UI-only boot cannot masquerade as a reader font.
  assert(manager.loadFamily(reader, renderer, 12));
  assert(SdCardFont::live == 1 && renderer.fonts.size() == 1);
  manager.unloadAll(renderer);
  assert(SdCardFont::live == 0);
  std::puts("PASS: fixed UI family, same-size isolation, reuse, switching and release");
}
