// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include <rex/ui/image_decode.h>

#include "input/bc3_encoder.h"
#include "track_textures.h"

namespace {

namespace tt = fm4_static_recomp::track_textures;
using fm4_static_recomp::signage::Image;

Image Pattern(uint32_t w, uint32_t h) {
  Image image{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      uint8_t* p = &image.rgba[(size_t(y) * w + x) * 4];
      p[0] = uint8_t(x * 7 + y * 3);
      p[1] = uint8_t(x * x + y);
      p[2] = uint8_t((x ^ y) * 13);
      p[3] = 255;
    }
  }
  return image;
}

}  // namespace

TEST_CASE("Texture sizes read and write as the settings take them", "[track_textures]") {
  CHECK(tt::ParseScale("default") == tt::Scale::kDefault);
  CHECK(tt::ParseScale("2x") == tt::Scale::k2x);
  CHECK(tt::ParseScale("4x") == tt::Scale::k4x);
  CHECK_FALSE(tt::ParseScale("3x"));
  CHECK(tt::ScaleName(tt::Scale::k4x) == "4x");
  CHECK(tt::ScaleLabel(tt::Scale::kDefault) == "DEFAULT");
  CHECK(tt::ScaleLabel(tt::Scale::k2x) == "2X");
}

TEST_CASE("A texture padded with its own wrapped edges, and cropped back", "[track_textures]") {
  const Image image = Pattern(16, 8);
  const Image padded = tt::WrapPad(image, 4);
  REQUIRE(padded.width == 24);
  REQUIRE(padded.height == 16);
  // The top-left corner of the pad is the image's bottom-right, as tiling has it.
  CHECK(padded.rgba[0] == image.rgba[((size_t(7 - 3) * 16) + 15 - 3) * 4]);
  CHECK(tt::Crop(padded, 4, 4, 16, 8).rgba == image.rgba);
}

TEST_CASE("Colours are matched to the reference's, alpha left alone", "[track_textures]") {
  Image dark{4, 4, std::vector<uint8_t>(64)}, light{4, 4, std::vector<uint8_t>(64)};
  for (size_t i = 0; i < 16; ++i) {
    dark.rgba[i * 4] = dark.rgba[i * 4 + 1] = dark.rgba[i * 4 + 2] = uint8_t(i);
    dark.rgba[i * 4 + 3] = 77;
    light.rgba[i * 4] = light.rgba[i * 4 + 1] = light.rgba[i * 4 + 2] = uint8_t(200 + i);
  }
  tt::MatchColours(dark, light);
  CHECK(dark.rgba[0] == 200);
  CHECK(dark.rgba[15 * 4] == 215);
  CHECK(dark.rgba[3] == 77);
}

TEST_CASE("A PNG the image decoder reads back as written", "[track_textures]") {
  const Image image = Pattern(37, 21);
  const std::vector<uint8_t> png = tt::EncodePng(image);
  int w = 0, h = 0;
  const std::vector<uint8_t> rgba = rex::ui::DecodeImageRGBA(png.data(), png.size(), w, h);
  CHECK(w == 37);
  CHECK(h == 21);
  CHECK(rgba == image.rgba);
}

TEST_CASE("A finished texture keeps the original's alpha, at the size asked, and round-trips on disk",
          "[track_textures]") {
  Image original = Pattern(16, 16);
  for (size_t i = 3; i < original.rgba.size(); i += 8) original.rgba[i] = 0;  // cut-out texels
  const uint32_t border = 4;
  const Image padded = tt::WrapPad(original, border);
  // A stand-in for the upscaler: each texel four times over.
  Image up{padded.width * 4, padded.height * 4, std::vector<uint8_t>(size_t(padded.width) * 16 * padded.height * 4)};
  for (uint32_t y = 0; y < up.height; ++y) {
    for (uint32_t x = 0; x < up.width; ++x) {
      for (int c = 0; c < 4; ++c) {
        up.rgba[(size_t(y) * up.width + x) * 4 + c] = padded.rgba[(size_t(y / 4) * padded.width + x / 4) * 4 + c];
      }
    }
  }
  for (const tt::Scale scale : {tt::Scale::k2x, tt::Scale::k4x}) {
    const auto image = tt::Finish(original, up, border, scale, true);
    REQUIRE(image);
    CHECK(image->width == 16 * uint32_t(scale));
    CHECK(image->format == rex::graphics::xenos::TextureFormat::k_DXT1);
    const auto back = tt::DeserializeImage(tt::SerializeImage(*image));
    REQUIRE(back);
    CHECK(back->width == image->width);
    CHECK(back->levels == image->levels);
    CHECK(back->data == image->data);
  }
  CHECK_FALSE(tt::DeserializeImage(std::vector<uint8_t>{1, 2, 3}));
}

TEST_CASE("Normal maps, flat colours and masks are not worth upscaling", "[track_textures]") {
  CHECK(tt::WorthUpscaling(Pattern(64, 64)));
  Image flat{64, 64, std::vector<uint8_t>(64 * 64 * 4, 90)};
  CHECK_FALSE(tt::WorthUpscaling(flat));
  Image normal = Pattern(64, 64);
  for (size_t i = 0; i < normal.rgba.size(); i += 4) {
    normal.rgba[i] = uint8_t(118 + (i / 4) % 20);
    normal.rgba[i + 1] = uint8_t(128 + (i / 4) % 17 - 8);
    normal.rgba[i + 2] = 240;
  }
  CHECK_FALSE(tt::WorthUpscaling(normal));
}

TEST_CASE("BC1 keeps cut-out texels transparent", "[track_textures]") {
  std::vector<uint8_t> rgba(4 * 4 * 4, 200);
  rgba[3] = 0;  // the first texel cut out
  const std::vector<uint8_t> blocks = fm4_static_recomp::input::EncodeBc1(rgba, 4, 4);
  REQUIRE(blocks.size() == 8);
  const uint16_t c0 = uint16_t(blocks[0] | blocks[1] << 8), c1 = uint16_t(blocks[2] | blocks[3] << 8);
  CHECK(c0 <= c1);               // three-colour mode
  CHECK((blocks[4] & 3) == 3);   // the cut-out texel: transparent black
  CHECK(((blocks[4] >> 2) & 3) != 3);
}

TEST_CASE("The texture settings' file: a line per setting not at default, read back", "[track_textures]") {
  tt::Settings settings;
  CHECK(tt::FormatSettings(settings).empty());
  settings.track = tt::Scale::k2x;
  settings.grass = tt::Scale::k4x;
  const std::string text = tt::FormatSettings(settings);
  CHECK(text == "fm4_track_textures = \"2x\"\nfm4_grass_texture = \"4x\"\n");
  CHECK(tt::ParseSettings(text) == settings);
  CHECK(tt::ParseSettings("fm4_road_surface = \"3x\"\r\nnonsense\nfm4_track_textures = 4x\r\n").track == tt::Scale::k4x);
  CHECK(tt::ParseSettings("fm4_road_surface = \"3x\"\n").road == tt::Scale::kDefault);
}
