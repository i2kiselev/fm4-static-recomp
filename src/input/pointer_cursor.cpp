// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "input/pointer_cursor.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace fm4_static_recomp::input {

namespace {

struct Vec {
  double x, y;
};

// The chevron, its tip at the origin, one unit tall: a slim left edge, the base notched deep.
constexpr std::array<Vec, 4> kArrow = {{{0, 0}, {0.119, 1.0}, {0.333, 0.643}, {0.762, 0.524}}};
constexpr double kHeight = 0.66;     // of the image: the arrow's height
constexpr double kOutline = 0.13;    // of the arrow's height: the edge's width (half each side of the outline)
constexpr double kShadow = 0.07;     // of the arrow's height: the shadow's offset, down and right
constexpr double kPad = 0.11;        // of the image: room for the edge round the tip
constexpr uint32_t kFill = 0xB4B4AF, kEdge = 0xD42E19;  // the game's grey and red (its menus' tiles and line)
constexpr double kShadowAlpha = 0.43;

bool Inside(const std::array<Vec, 4>& p, Vec q) {
  bool in = false;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
    if ((p[i].y > q.y) != (p[j].y > q.y) && q.x < (p[j].x - p[i].x) * (q.y - p[i].y) / (p[j].y - p[i].y) + p[i].x) {
      in = !in;
    }
  }
  return in;
}

double EdgeDistance(const std::array<Vec, 4>& p, Vec q) {
  double best = 1e9;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
    const Vec a = p[j], b = p[i];
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double t = std::clamp(((q.x - a.x) * dx + (q.y - a.y) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
    best = std::min(best, std::hypot(q.x - (a.x + t * dx), q.y - (a.y + t * dy)));
  }
  return best;
}

}  // namespace

int PointerSize(int window_height) {
  const int size = int(std::lround(40.0 * std::max(window_height, 720) / 1080.0 / 8.0)) * 8;
  return std::clamp(size, 32, 128);
}

PointerImage DrawPointer(int size) {
  PointerImage image;
  size = std::clamp(size, 16, 256);
  image.size = size;
  const double scale = size * kHeight, pad = size * kPad;
  image.hot_x = int(std::lround(pad)), image.hot_y = int(std::lround(pad));
  std::array<Vec, 4> arrow;
  for (size_t i = 0; i < arrow.size(); ++i) arrow[i] = {pad + kArrow[i].x * scale, pad + kArrow[i].y * scale};
  const double half = kOutline * scale / 2, shadow = kShadow * scale;
  image.argb.assign(size_t(size) * size, 0);
  constexpr int kSamples = 4;  // per axis
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      // Each sample: the edge, the fill, the shadow under them, or nothing; summed premultiplied.
      double a = 0, r = 0, g = 0, b = 0;
      for (int sy = 0; sy < kSamples; ++sy) {
        for (int sx = 0; sx < kSamples; ++sx) {
          const Vec q{x + (sx + 0.5) / kSamples, y + (sy + 0.5) / kSamples};
          uint32_t colour = 0;
          double alpha = 0;
          if (EdgeDistance(arrow, q) <= half) {
            colour = kEdge, alpha = 1;
          } else if (Inside(arrow, q)) {
            colour = kFill, alpha = 1;
          } else {
            const Vec s{q.x - shadow, q.y - shadow};
            if (Inside(arrow, s) || EdgeDistance(arrow, s) <= half) colour = 0, alpha = kShadowAlpha;
          }
          a += alpha;
          r += alpha * ((colour >> 16) & 0xFF), g += alpha * ((colour >> 8) & 0xFF), b += alpha * (colour & 0xFF);
        }
      }
      if (a <= 0) continue;
      const double n = kSamples * kSamples;
      const uint32_t alpha = uint32_t(std::lround(255 * a / n));
      // Straight alpha: the colour of what covers the pixel.
      image.argb[size_t(y) * size + x] = alpha << 24 | uint32_t(std::lround(r / a)) << 16 |
                                         uint32_t(std::lround(g / a)) << 8 | uint32_t(std::lround(b / a));
    }
  }
  return image;
}

}  // namespace fm4_static_recomp::input
