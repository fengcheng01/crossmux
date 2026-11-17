#include <cassert>
#include <cstdio>
#include "DirectGlyphSmoothing.h"

int main() {
  using directGlyphSmoothing::softenCorner;
  assert(softenCorner(3, 0, 3, 0, 3) == 2);
  assert(softenCorner(3, 0, 3, 3, 3) == 3);
  assert(softenCorner(3, 3, 3, 0, 3) == 3);
  assert(softenCorner(2, 0, 3, 0, 3) == 2);
  assert(softenCorner(0, 0, 3, 0, 3) == 0);
  assert(softenCorner(3, 0, 0, 0, 0) == 3);  // Isolated dot or one-pixel diagonal.
  assert(softenCorner(3, 0, 0, 3, 3) == 3);  // Vertical one-pixel stem.
  assert(softenCorner(3, 3, 3, 0, 0) == 3);  // Horizontal one-pixel stem.
  assert(softenCorner(3, 3, 3, 3, 3) == 3);  // Solid stroke interior.
  assert(softenCorner(1, 0, 3, 0, 3) == 1);  // Existing light AA is not erased.
  std::printf("PASS: shared TTF/cpfont corner smoothing preserves gray and white endpoints\n");
}
