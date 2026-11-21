// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

#include "input/pointer_cursor.h"

using namespace fm4_static_recomp::input;

namespace {

uint32_t At(const PointerImage& image, int x, int y) { return image.argb[size_t(y) * image.size + x]; }
uint32_t Alpha(uint32_t argb) { return argb >> 24; }
uint32_t Grey(uint32_t argb) { return argb & 0xFF; }

}  // namespace

TEST_CASE("Pointer: the arrow's tip is the hot spot, edged red", "[pointer_cursor]") {
  const PointerImage image = DrawPointer(32);
  REQUIRE(image.size == 32);
  REQUIRE(image.argb.size() == 32u * 32u);
  CHECK(image.hot_x == image.hot_y);
  CHECK(image.hot_x >= 2);
  const uint32_t tip = At(image, image.hot_x, image.hot_y);
  CHECK(Alpha(tip) > 100);
  CHECK(((tip >> 16) & 0xFF) > 0x90);  // red
  CHECK(Grey(tip) < 0x60);
  // Nothing up and to the left of the tip but the outline's edge.
  CHECK(Alpha(At(image, 0, 0)) == 0);
}

TEST_CASE("Pointer: light inside, clear outside", "[pointer_cursor]") {
  const PointerImage image = DrawPointer(32);
  // Inside, half way down: the grey fill.
  const uint32_t inside = At(image, image.hot_x + 3, image.hot_y + 10);
  CHECK(Alpha(inside) == 255);
  CHECK(Grey(inside) > 0x90);
  CHECK(Grey(inside) < 0xC8);
  // Right of the arrow near the top, and the bottom right corner: clear.
  CHECK(Alpha(At(image, 28, 4)) == 0);
  CHECK(Alpha(At(image, 31, 31)) == 0);
}

TEST_CASE("Pointer: bigger screens get a bigger arrow of the same shape", "[pointer_cursor]") {
  const PointerImage small = DrawPointer(32), big = DrawPointer(64);
  CHECK(big.size == 64);
  CHECK(std::abs(big.hot_x - small.hot_x * 2) <= 1);  // the tip's padding, rounded to whole pixels
  // The same spot, scaled: filled in both.
  CHECK(Alpha(At(big, big.hot_x + 6, big.hot_y + 20)) == 255);
  // Held to sane sizes.
  CHECK(DrawPointer(4).size == 16);
}
