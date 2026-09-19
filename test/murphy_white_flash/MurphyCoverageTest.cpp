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
  std::printf("PASS: shared TTF/cpfont corner smoothing preserves gray and white endpoints\n");
}
