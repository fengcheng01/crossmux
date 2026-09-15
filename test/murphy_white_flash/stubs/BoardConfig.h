#pragma once
#include <cstdint>
namespace BoardConfig {
enum class Board { Sticky, MurphyM4, WaveshareEpaper397, XteinkX4Pro, XteinkX4 };
struct Profile {
  uint16_t displayWidth = 16;
  uint16_t displayHeight = 2;
  uint32_t displaySpiHz = 40000000;
  struct { bool mirrorX = false; bool mirrorY = false; } orientation;
  Board board = Board::MurphyM4;
};
inline constexpr Profile ACTIVE{};
}
