#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "input/bc3_encoder.h"

using fm4_static_recomp::input::EncodeBc3;

namespace {

std::array<int, 3> From565(uint16_t v) {
  return {(v >> 11) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31};
}

// The block's texels, RGBA, as D3D decodes BC3.
std::array<std::array<int, 4>, 16> Decode(const uint8_t* block) {
  const int a0 = block[0], a1 = block[1];
  std::array<int, 8> alphas{a0, a1};
  for (int k = 2; k < 8; ++k) {
    alphas[k] = a0 > a1 ? ((8 - k) * a0 + (k - 1) * a1) / 7
                : k < 6 ? ((6 - k) * a0 + (k - 1) * a1) / 5
                        : (k == 6 ? 0 : 255);
  }
  uint64_t alpha_bits = 0;
  for (int i = 0; i < 6; ++i)
    alpha_bits |= uint64_t(block[2 + i]) << (8 * i);
  const auto c0 = From565(uint16_t(block[8] | block[9] << 8));
  const auto c1 = From565(uint16_t(block[10] | block[11] << 8));
  std::array<std::array<int, 3>, 4> colors{c0, c1};
  for (int c = 0; c < 3; ++c) {
    colors[2][c] = (2 * c0[c] + c1[c]) / 3;
    colors[3][c] = (c0[c] + 2 * c1[c]) / 3;
  }
  const uint32_t color_bits = block[12] | block[13] << 8 | block[14] << 16 | uint32_t(block[15]) << 24;
  std::array<std::array<int, 4>, 16> texels;
  for (int i = 0; i < 16; ++i) {
    const auto& color = colors[(color_bits >> (2 * i)) & 3];
    texels[i] = {color[0], color[1], color[2], alphas[(alpha_bits >> (3 * i)) & 7]};
  }
  return texels;
}

std::vector<uint8_t> Image(uint32_t width, uint32_t height, auto texel) {
  std::vector<uint8_t> rgba(size_t(width) * height * 4);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const std::array<uint8_t, 4> value = texel(x, y);
      std::copy(value.begin(), value.end(), &rgba[(size_t(y) * width + x) * 4]);
    }
  }
  return rgba;
}

}  // namespace

TEST_CASE("BC3 blocks follow the image row by row", "[bc3_encoder]") {
  // Left half black, right half white, both opaque.
  const auto rgba = Image(8, 4, [](uint32_t x, uint32_t) -> std::array<uint8_t, 4> {
    const uint8_t v = x < 4 ? 0 : 255;
    return {v, v, v, 255};
  });
  const auto blocks = EncodeBc3(rgba, 8, 4);
  REQUIRE(blocks.size() == 32);
  for (const auto& texel : Decode(blocks.data()))
    CHECK(texel == std::array<int, 4>{0, 0, 0, 255});
  for (const auto& texel : Decode(blocks.data() + 16))
    CHECK(texel == std::array<int, 4>{255, 255, 255, 255});
}

TEST_CASE("BC3 keeps a keycap's two greys and its transparent corner", "[bc3_encoder]") {
  const auto rgba = Image(4, 4, [](uint32_t x, uint32_t y) -> std::array<uint8_t, 4> {
    if (x == 0 && y == 0)
      return {0, 0, 0, 0};
    const uint8_t v = y < 2 ? 0xE6 : 0x1A;
    return {v, v, v, 255};
  });
  const auto texels = Decode(EncodeBc3(rgba, 4, 4).data());
  CHECK(texels[0][3] == 0);
  for (int i = 1; i < 16; ++i) {
    INFO(i);
    CHECK(texels[i][3] == 255);
    CHECK(std::abs(texels[i][0] - rgba[size_t(i) * 4]) <= 4);
    CHECK(texels[i][0] == texels[i][1]);
  }
}

TEST_CASE("BC3 alpha ramps stay within a step", "[bc3_encoder]") {
  const auto rgba = Image(4, 4, [](uint32_t x, uint32_t y) -> std::array<uint8_t, 4> {
    return {255, 255, 255, uint8_t((y * 4 + x) * 17)};
  });
  const auto texels = Decode(EncodeBc3(rgba, 4, 4).data());
  for (int i = 0; i < 16; ++i) {
    INFO(i);
    CHECK(std::abs(texels[i][3] - i * 17) <= 255 / 14 + 1);
  }
  CHECK(texels[0][3] == 0);
  CHECK(texels[15][3] == 255);
}
