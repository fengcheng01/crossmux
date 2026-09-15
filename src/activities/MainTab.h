#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

enum class MainTab : uint8_t { None, Recent, Library, Apps, Settings, Statistics };
enum class MainTabFocus : uint8_t { Tabs, Content };
enum class MainTabContentEdge : uint8_t { First, Last };

namespace MainTabs {
#if FREEINK_DEVICE_MURPHY_M4
inline constexpr std::array<MainTab, 5> values = {MainTab::Recent, MainTab::Library, MainTab::Statistics,
                                                  MainTab::Settings, MainTab::Apps};
#else
inline constexpr std::array<MainTab, 5> values = {MainTab::Library, MainTab::Apps, MainTab::Recent, MainTab::Settings,
                                                  MainTab::Statistics};
#endif

using Order = std::array<MainTab, 5>;
const Order& orderedValues();

constexpr bool isValid(const Order& order) {
  for (size_t i = 0; i < order.size(); ++i) {
    bool found = false;
    for (const auto tab : values)
      if (order[i] == tab) found = true;
    if (!found) return false;
    for (size_t j = 0; j < i; ++j)
      if (order[i] == order[j]) return false;
  }
  return true;
}

constexpr bool moveTo(Order& order, const int source, const int destination) {
  const int count = static_cast<int>(order.size());
  if (source < 0 || source >= count || destination < 0 || destination >= count || !isValid(order)) return false;
  const auto tab = order[source];
  if (source < destination) {
    for (int i = source; i < destination; ++i) order[i] = order[i + 1];
  } else {
    for (int i = source; i > destination; --i) order[i] = order[i - 1];
  }
  order[destination] = tab;
  return true;
}

constexpr int indexOf(const MainTab tab, const Order& order = values) {
  for (size_t i = 0; i < order.size(); ++i) {
    if (order[i] == tab) return static_cast<int>(i);
  }
  return -1;
}

constexpr MainTab adjacent(const MainTab tab, const int direction, const Order& order = values) {
  const int index = indexOf(tab, order);
  if (index < 0) return MainTab::None;
  const int count = static_cast<int>(values.size());
  return order[(index + (direction < 0 ? count - 1 : 1)) % count];
}

constexpr MainTab fromX(const int x, const int width, const Order& order = values) {
  if (x < 0 || width <= 0 || x >= width) return MainTab::None;
  const int index = x * static_cast<int>(values.size()) / width;
  return order[index];
}

using Ranks = std::array<uint8_t, values.size()>;

constexpr Ranks ranksFor(const Order& order) {
  Ranks ranks{};
  for (size_t i = 0; i < values.size(); ++i) ranks[i] = static_cast<uint8_t>(indexOf(values[i], order) + 1);
  return ranks;
}

constexpr bool orderFromRanks(const Ranks& ranks, Order& result) {
  Order candidate{};
  for (size_t i = 0; i < ranks.size(); ++i) {
    if (ranks[i] < 1 || ranks[i] > ranks.size()) return false;
    const size_t slot = ranks[i] - 1;
    if (candidate[slot] != MainTab::None) return false;
    candidate[slot] = values[i];
  }
  result = candidate;
  return true;
}

constexpr MainTab backTarget(const MainTab tab) { return tab == MainTab::Recent ? MainTab::None : MainTab::Recent; }

constexpr int contentEdgeIndex(const MainTabContentEdge edge, const int count) {
  if (count <= 0) return 0;
  switch (edge) {
    case MainTabContentEdge::First:
      return 0;
    case MainTabContentEdge::Last:
      return count - 1;
  }
  return 0;
}
}  // namespace MainTabs
