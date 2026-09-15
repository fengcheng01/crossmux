#include "activities/MainTab.h"

int main() {
  constexpr std::array<MainTab, 5> expected = {MainTab::Recent, MainTab::Library, MainTab::Statistics,
                                               MainTab::Settings, MainTab::Apps};
  if (MainTabs::values != expected) return 1;
  for (const int width : {480, 800}) {
    for (int slot = 0; slot < 5; ++slot) {
      const auto tab = expected[slot];
      if (MainTabs::indexOf(tab) != slot) return 2;
      if (MainTabs::adjacent(tab, 1) != expected[(slot + 1) % 5]) return 3;
      if (MainTabs::adjacent(tab, -1) != expected[(slot + 4) % 5]) return 4;
      for (int x = width * slot / 5; x < width * (slot + 1) / 5; ++x) {
        if (MainTabs::fromX(x, width) != tab) return 5;
      }
    }
    if (MainTabs::fromX(-1, width) != MainTab::None || MainTabs::fromX(width, width) != MainTab::None) return 6;
  }
  if (MainTabs::fromX(0, 0) != MainTab::None) return 7;
  return 0;
}
