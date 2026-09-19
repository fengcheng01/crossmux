#pragma once
#include <cstdint>

namespace directGlyphSmoothing {
constexpr uint8_t softenCorner(uint8_t center, uint8_t left, uint8_t right, uint8_t up, uint8_t down) {
  if (center != 3) return center;  // Preserve existing AA shades and empty background.
  // TTF edges may already contain a light-gray neighbor (coverage 1), while
  // cpfont edges use only 0/3. Treat either as ink on the far side of a step.
  const bool horizontalStep = (left == 0 && right >= 1) || (right == 0 && left >= 1);
  const bool verticalStep = (up == 0 && down >= 1) || (down == 0 && up >= 1);
  // Only a supported convex corner becomes dark gray. Straight stems, isolated
  // dots, one-pixel diagonals and solid interiors keep their original coverage.
  return horizontalStep && verticalStep ? 2 : center;
}
inline uint8_t sample(const uint8_t* bitmap, int width, int height, int x, int y, bool twoBit) {
  if (x < 0 || y < 0 || x >= width || y >= height) return 0;
  const unsigned pos = static_cast<unsigned>(y * width + x);
  return twoBit ? static_cast<uint8_t>((bitmap[pos >> 2] >> (6 - 2 * (pos & 3))) & 3)
                : static_cast<uint8_t>(((bitmap[pos >> 3] >> (7 - (pos & 7))) & 1) * 3);
}
inline uint8_t coverage(const uint8_t* bitmap, int width, int height, int x, int y, bool twoBit) {
  const uint8_t center = sample(bitmap, width, height, x, y, twoBit);
  if (center != 3) return center;
  const auto left = sample(bitmap, width, height, x - 1, y, twoBit);
  const auto right = sample(bitmap, width, height, x + 1, y, twoBit);
  if (!((left == 0 && right >= 1) || (right == 0 && left >= 1))) return center;
  return softenCorner(center, left, right, sample(bitmap, width, height, x, y - 1, twoBit),
                      sample(bitmap, width, height, x, y + 1, twoBit));
}
// Absolute gray planes store 1 for ink. The next B/W baseline stores 0 for any ink.
constexpr uint8_t binaryBaseline(uint8_t lsb, uint8_t msb) { return static_cast<uint8_t>(~(lsb | msb)); }
}  // namespace directGlyphSmoothing
