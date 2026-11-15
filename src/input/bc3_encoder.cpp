// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "input/bc3_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace fm4_static_recomp::input {
namespace {

uint16_t To565(const std::array<int, 3>& c) {
  return uint16_t((std::lround(c[0] * 31 / 255.0) << 11) | (std::lround(c[1] * 63 / 255.0) << 5) |
                  std::lround(c[2] * 31 / 255.0));
}

std::array<int, 3> From565(uint16_t v) {
  return {(v >> 11) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31};
}

void EncodeAlpha(const std::array<const uint8_t*, 16>& pixels, uint8_t* out) {
  uint8_t low = 255, high = 0;
  for (const uint8_t* p : pixels) {
    low = std::min(low, p[3]);
    high = std::max(high, p[3]);
  }
  out[0] = high;
  out[1] = low;
  uint64_t indices = 0;
  if (high != low) {
    // With alpha0 > alpha1 the palette is alpha0 (index 0), alpha1 (1) and
    // six steps between, index k at (8 - k) sevenths from alpha1.
    for (size_t i = 0; i < 16; ++i) {
      const long step = std::lround((pixels[i][3] - low) * 7.0 / (high - low));
      const uint64_t index = step == 7 ? 0 : step == 0 ? 1 : uint64_t(8 - step);
      indices |= index << (3 * i);
    }
  }
  for (size_t i = 0; i < 6; ++i)
    out[2 + i] = uint8_t(indices >> (8 * i));
}

void EncodeColor(const std::array<const uint8_t*, 16>& pixels, uint8_t* out) {
  // Fully transparent texels keep no colour worth matching.
  std::array<int, 3> low{255, 255, 255}, high{0, 0, 0};
  bool any = false;
  for (const uint8_t* p : pixels) {
    if (!p[3])
      continue;
    any = true;
    for (size_t c = 0; c < 3; ++c) {
      low[c] = std::min(low[c], int(p[c]));
      high[c] = std::max(high[c], int(p[c]));
    }
  }
  if (!any)
    low = high = {0, 0, 0};
  uint16_t c0 = To565(high), c1 = To565(low);
  if (c0 < c1)
    std::swap(c0, c1);
  const auto e0 = From565(c0), e1 = From565(c1);
  std::array<std::array<int, 3>, 4> palette{e0, e1};
  for (size_t c = 0; c < 3; ++c) {
    palette[2][c] = (2 * e0[c] + e1[c]) / 3;
    palette[3][c] = (e0[c] + 2 * e1[c]) / 3;
  }
  uint32_t indices = 0;
  if (c0 != c1) {
    for (size_t i = 0; i < 16; ++i) {
      uint32_t best = 0;
      int best_distance = INT32_MAX;
      for (uint32_t k = 0; k < 4; ++k) {
        int distance = 0;
        for (size_t c = 0; c < 3; ++c) {
          const int d = int(pixels[i][c]) - palette[k][c];
          distance += d * d;
        }
        if (distance < best_distance) {
          best_distance = distance;
          best = k;
        }
      }
      indices |= best << (2 * i);
    }
  }
  out[0] = uint8_t(c0);
  out[1] = uint8_t(c0 >> 8);
  out[2] = uint8_t(c1);
  out[3] = uint8_t(c1 >> 8);
  for (size_t i = 0; i < 4; ++i)
    out[4 + i] = uint8_t(indices >> (8 * i));
}

}  // namespace

std::vector<uint8_t> EncodeBc3(std::span<const uint8_t> rgba, uint32_t width, uint32_t height) {
  std::vector<uint8_t> blocks(size_t(width / 4) * (height / 4) * 16);
  uint8_t* out = blocks.data();
  for (uint32_t by = 0; by < height / 4; ++by) {
    for (uint32_t bx = 0; bx < width / 4; ++bx, out += 16) {
      std::array<const uint8_t*, 16> pixels;
      for (uint32_t i = 0; i < 16; ++i)
        pixels[i] = &rgba[((size_t(by) * 4 + i / 4) * width + bx * 4 + i % 4) * 4];
      EncodeAlpha(pixels, out);
      EncodeColor(pixels, out + 8);
    }
  }
  return blocks;
}

}  // namespace fm4_static_recomp::input
