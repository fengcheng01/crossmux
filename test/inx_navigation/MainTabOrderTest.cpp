#include <algorithm>

#include "activities/MainTab.h"

int main() {
  auto order = MainTabs::values;
  std::sort(order.begin(), order.end());
  int permutations = 0;
  do {
    if (!MainTabs::isValid(order)) return 1;
    MainTabs::Order roundTrip{};
    if (!MainTabs::orderFromRanks(MainTabs::ranksFor(order), roundTrip) || roundTrip != order) return 15;
    for (int slot = 0; slot < 5; ++slot) {
      if (MainTabs::indexOf(order[slot], order) != slot) return 2;
      if (MainTabs::adjacent(order[slot], 1, order) != order[(slot + 1) % 5]) return 3;
      if (MainTabs::adjacent(order[slot], -1, order) != order[(slot + 4) % 5]) return 4;
      for (const int width : {480, 800}) {
        for (int x = width * slot / 5; x < width * (slot + 1) / 5; ++x)
          if (MainTabs::fromX(x, width, order) != order[slot]) return 5;
      }
      for (int destination = 0; destination < 5; ++destination) {
        auto moved = order;
        if (!MainTabs::moveTo(moved, slot, destination) || !MainTabs::isValid(moved)) return 6;
        if (moved[destination] != order[slot]) return 7;
        int next = 0;
        for (int i = 0; i < 5; ++i) {
          if (i == destination) continue;
          if (next == slot) ++next;
          if (moved[i] != order[next++]) return 8;
        }
      }
    }
    ++permutations;
  } while (std::next_permutation(order.begin(), order.end()));
  if (permutations != 120) return 9;
  order = MainTabs::values;
  if (MainTabs::moveTo(order, -1, 0) || MainTabs::moveTo(order, 0, 5) || order != MainTabs::values) return 10;
  order[0] = order[1];
  if (MainTabs::isValid(order) || MainTabs::moveTo(order, 0, 1)) return 11;
  order = MainTabs::values;
  order[0] = MainTab::None;
  if (MainTabs::isValid(order)) return 12;
  order[0] = static_cast<MainTab>(255);
  if (MainTabs::isValid(order)) return 13;
  if (MainTabs::fromX(-1, 480) != MainTab::None || MainTabs::fromX(480, 480) != MainTab::None ||
      MainTabs::fromX(0, 0) != MainTab::None)
    return 14;
  auto ranks = MainTabs::ranksFor(MainTabs::values);
  auto result = MainTabs::values;
  ranks[0] = 5;
  for (size_t i = 1; i < ranks.size(); ++i)
    if (ranks[i] != i + 1) return 16;
  if (MainTabs::orderFromRanks(ranks, result) || result != MainTabs::values) return 17;
  ranks[4] = 1;
  if (!MainTabs::orderFromRanks(ranks, result) || result[0] != MainTabs::values[4] || result[4] != MainTabs::values[0])
    return 18;
  const auto saved = result;
  ranks[2] = 0;
  if (MainTabs::orderFromRanks(ranks, result) || result != saved) return 19;
  ranks[2] = 6;
  if (MainTabs::orderFromRanks(ranks, result) || result != saved) return 20;
  return 0;
}
