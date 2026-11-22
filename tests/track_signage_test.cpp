// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <rex/hash.h>

#include "track_signage.h"
#include "ui/hostui/ui_archive.h"

namespace signage = fm4_static_recomp::signage;

namespace {

signage::Image Solid(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
  signage::Image image{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
  for (size_t i = 0; i < image.rgba.size(); i += 4) {
    image.rgba[i] = r;
    image.rgba[i + 1] = g;
    image.rgba[i + 2] = b;
    image.rgba[i + 3] = a;
  }
  return image;
}

// Bytes of a DXT5 chain from w x h down to 1x1 (each level whole 4x4 blocks).
size_t ChainBytes(uint32_t w, uint32_t h, uint32_t& levels) {
  size_t bytes = 0;
  levels = 0;
  for (;;) {
    bytes += size_t((w + 3) / 4) * ((h + 3) / 4) * 16;
    ++levels;
    if (w == 1 && h == 1) return bytes;
    w = w > 1 ? w / 2 : 1;
    h = h > 1 ? h / 2 : 1;
  }
}

const signage::Track& Hockenheim() {
  const auto* track = signage::FindTrack("hockenheim");
  REQUIRE(track);
  return *track;
}

}  // namespace

TEST_CASE("Hockenheim lists its road, grass and 31 boards, each its own texture and name", "[signage]") {
  const auto& track = Hockenheim();
  CHECK(track.name == "HOCKENHEIMRING");
  CHECK(track.boards.size() == 33);
  std::set<std::string> keys, names;
  std::set<uint64_t> hashes;
  std::set<uint32_t> textures;
  for (const auto& board : track.boards) {
    CHECK(keys.insert(std::string(board.key)).second);
    CHECK(names.insert(std::string(board.name)).second);
    CHECK(hashes.insert(board.hash).second);
    CHECK(textures.insert(board.texture).second);
    CHECK(board.width % 4 == 0);
    CHECK(board.height % 4 == 0);
    CHECK(board.levels >= 1);
    // The SIGNAGE page's description heading fits about 30 capitals at 1080p.
    CHECK(board.name.size() <= 30);
  }
  CHECK(signage::FindBoard(track, "mobil1")->hash == 0xCE9A5CD46ACED489);
  CHECK_FALSE(signage::FindBoard(track, "Mobil1"));
  CHECK(signage::ChoiceKey(track, *signage::FindBoard(track, "mobil1")) == "hockenheim.mobil1");
}

TEST_CASE("Every board's hash is the texture's in the game's track archive", "[signage]") {
  const char* root = std::getenv("FM4_TEST_GAME_ROOT");
  const std::filesystem::path game = root ? root : FM4_TEST_GAME_ROOT;
  for (const auto& track : signage::Tracks()) {
    const auto archive = fm4_static_recomp::hostui::UiArchive::Open(game / std::string(track.archive));
    if (!archive) {
      SKIP("No game data; set FM4_TEST_GAME_ROOT to check the boards against it");
    }
    for (const auto& board : track.boards) {
      char member[32];
      std::snprintf(member, sizeof(member), "_0x%08X_B.bix", board.texture);
      const auto data = archive->Read(member);
      INFO(board.key);
      REQUIRE(data);
      // The SDK's texture replacement hashes the base level as the game loads it: this file.
      CHECK(XXH3_64bits(data->data(), data->size()) == board.hash);
      // A tiled DXT5 level of the board's size is a multiple of 4 KB (32 x 32 blocks of 16 bytes).
      CHECK(data->size() >= size_t(board.width) * board.height);
      const auto image = signage::DecodeTiledDxt5(*data, board.width, board.height);
      CHECK(image.width == board.width);
      CHECK(image.height == board.height);
    }
  }
}

TEST_CASE("signage.toml keeps a picture per board, and only boards that exist", "[signage]") {
  const signage::Choices choices{{"hockenheim.mobil1", "My Logo.png"}, {"hockenheim.santander-wall", "a\"b.png"}};
  const std::string text = signage::FormatChoices(choices);
  CHECK(text.find("hockenheim.mobil1 = \"My Logo.png\"\n") != std::string::npos);
  CHECK(signage::ParseChoices(text) == choices);
  const auto parsed = signage::ParseChoices(
      "\xEF\xBB\xBF# comment\r\n"
      "hockenheim.mobil1 = \"logo.png\"\r\n"
      "hockenheim.no-such-board = \"x.png\"\n"
      "nowhere.mobil1 = \"x.png\"\n"
      "hockenheim.kiosk = \"..\\\\up.png\"\n"   // not a plain file name
      "hockenheim.dunlop = unquoted.png\n"
      "fm4_track_signage = false\n");
  CHECK(parsed == signage::Choices{{"hockenheim.mobil1", "logo.png"}});
}

TEST_CASE("The scale is the largest the picture covers, from 1 to 4", "[signage]") {
  const auto& board = *signage::FindBoard(Hockenheim(), "mobil1");  // 1024 x 256
  CHECK(signage::ScaleFor(board, 100, 30) == 1);
  CHECK(signage::ScaleFor(board, 1024, 256) == 1);
  CHECK(signage::ScaleFor(board, 2047, 512) == 1);
  CHECK(signage::ScaleFor(board, 2048, 512) == 2);
  CHECK(signage::ScaleFor(board, 4096, 400) == 1);  // both sizes must reach it
  CHECK(signage::ScaleFor(board, 4096, 1024) == 4);
  CHECK(signage::ScaleFor(board, 16384, 4096) == 4);
}

TEST_CASE("Resampling keeps a flat colour, previews keep the shape", "[signage]") {
  const auto flat = signage::Resample(Solid(3, 2, 10, 20, 30, 40), 8, 5);
  REQUIRE(flat.rgba.size() == 8 * 5 * 4);
  for (size_t i = 0; i < flat.rgba.size(); i += 4) {
    CHECK(flat.rgba[i] == 10);
    CHECK(flat.rgba[i + 3] == 40);
  }
  const auto fitted = signage::Fit(Solid(2048, 256, 1, 2, 3), 512, 256);
  CHECK(fitted.width == 512);
  CHECK(fitted.height == 64);
  const auto small = signage::Fit(Solid(100, 50, 1, 2, 3), 512, 256);
  CHECK(small.width == 100);  // never enlarged
}

TEST_CASE("A board's picture is DXT5 at its scale, every level down to 1x1", "[signage]") {
  const auto& board = *signage::FindBoard(Hockenheim(), "ring-logo");  // 256 x 64, 9 levels
  uint32_t levels = 0;
  const auto one = signage::BuildImage(board, Solid(256, 64, 255, 0, 0));
  CHECK(one->format == rex::graphics::xenos::TextureFormat::k_DXT4_5);
  CHECK(one->width == 256);
  CHECK(one->height == 64);
  CHECK(one->data.size() == ChainBytes(256, 64, levels));
  CHECK(one->levels == levels);
  CHECK(one->levels >= board.levels);
  const auto two = signage::BuildImage(board, Solid(600, 140, 0, 0, 255));
  CHECK(two->width == 512);
  CHECK(two->height == 128);
  CHECK(two->data.size() == ChainBytes(512, 128, levels));
  CHECK(two->levels == board.levels + 1);
}

TEST_CASE("A tiled DXT5 block decodes to its colours", "[signage]") {
  // One 4x4 block (a 4x4 texture is its first block, at offset 0), in the console's byte order
  // (16-bit words swapped): alpha 255 everywhere, colour 0 red (0xF800) everywhere.
  std::vector<uint8_t> data(4096, 0);
  const uint8_t pc[16] = {255, 255, 0, 0, 0, 0, 0, 0, 0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0};
  for (int k = 0; k < 16; k += 2) {
    data[k] = pc[k + 1];
    data[k + 1] = pc[k];
  }
  const auto image = signage::DecodeTiledDxt5(data, 4, 4);
  REQUIRE(image.width == 4);
  for (size_t i = 0; i < image.rgba.size(); i += 4) {
    CHECK(image.rgba[i] == 255);
    CHECK(image.rgba[i + 1] == 0);
    CHECK(image.rgba[i + 2] == 0);
    CHECK(image.rgba[i + 3] == 255);
  }
}
