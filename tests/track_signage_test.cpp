// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

#include "track_signage.h"

namespace signage = fm4_static_recomp::signage;

namespace {

std::vector<uint8_t> Solid(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
  std::vector<uint8_t> out(size_t(w) * h * 4);
  for (size_t i = 0; i < out.size(); i += 4) {
    out[i] = r;
    out[i + 1] = g;
    out[i + 2] = b;
    out[i + 3] = a;
  }
  return out;
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

}  // namespace

TEST_CASE("Every board has its own file name and texture", "[signage]") {
  std::set<std::string> names;
  std::set<uint64_t> hashes;
  for (const auto& board : signage::Boards()) {
    CHECK(names.insert(std::string(board.file)).second);
    CHECK(hashes.insert(board.hash).second);
    CHECK(board.width % 4 == 0);
    CHECK(board.height % 4 == 0);
    CHECK_FALSE(board.what.empty());
  }
  CHECK(signage::Boards().size() >= 9);
}

TEST_CASE("A PNG's name finds its board, case aside", "[signage]") {
  const auto* mobil = signage::FindBoard("hockenheim-mobil1");
  REQUIRE(mobil);
  CHECK(mobil->hash == 0xCE9A5CD46ACED489);
  CHECK(signage::FindBoard("Hockenheim-MOBIL1") == mobil);
  CHECK_FALSE(signage::FindBoard("hockenheim-mobil"));
  CHECK_FALSE(signage::FindBoard("hockenheim-mobil1.png"));
  CHECK_FALSE(signage::FindBoard(""));
}

TEST_CASE("The scale is the largest the image covers, from 1 to 4", "[signage]") {
  const auto& board = *signage::FindBoard("hockenheim-mobil1");  // 1024 x 256
  CHECK(signage::ScaleFor(board, 100, 30) == 1);
  CHECK(signage::ScaleFor(board, 1024, 256) == 1);
  CHECK(signage::ScaleFor(board, 2047, 512) == 1);
  CHECK(signage::ScaleFor(board, 2048, 512) == 2);
  CHECK(signage::ScaleFor(board, 4096, 400) == 1);  // both sizes must reach it
  CHECK(signage::ScaleFor(board, 4096, 1024) == 4);
  CHECK(signage::ScaleFor(board, 16384, 4096) == 4);
}

TEST_CASE("Resampling keeps a flat colour and the corners", "[signage]") {
  const auto flat = signage::Resample(Solid(3, 2, 10, 20, 30, 40), 3, 2, 8, 5);
  REQUIRE(flat.size() == 8 * 5 * 4);
  for (size_t i = 0; i < flat.size(); i += 4) {
    CHECK(flat[i] == 10);
    CHECK(flat[i + 3] == 40);
  }
  std::vector<uint8_t> two(2 * 1 * 4, 0);
  two[0] = 0;
  two[4] = 200;  // left black, right red-ish
  for (int i : {3, 7}) two[size_t(i)] = 255;
  const auto wide = signage::Resample(two, 2, 1, 4, 1);
  CHECK(wide[0] == 0);
  CHECK(wide[12] == 200);
  CHECK(wide[4] > 0);
  CHECK(wide[4] < 200);
}

TEST_CASE("A board's image is DXT5 at its scale, every level down to 1x1", "[signage]") {
  const auto& board = *signage::FindBoard("hockenheim-ring-logo");  // 256 x 64, 9 levels
  uint32_t levels = 0;
  const auto one = signage::BuildImage(board, Solid(256, 64, 255, 0, 0), 256, 64);
  CHECK(one->format == rex::graphics::xenos::TextureFormat::k_DXT4_5);
  CHECK(one->width == 256);
  CHECK(one->height == 64);
  CHECK(one->data.size() == ChainBytes(256, 64, levels));
  CHECK(one->levels == levels);
  CHECK(one->levels >= board.levels);
  const auto two = signage::BuildImage(board, Solid(600, 140, 0, 0, 255), 600, 140);
  CHECK(two->width == 512);
  CHECK(two->height == 128);
  CHECK(two->data.size() == ChainBytes(512, 128, levels));
  CHECK(two->levels == board.levels + 1);
}

TEST_CASE("README.txt names every board with its size", "[signage]") {
  const std::string readme = signage::Readme();
  for (const auto& board : signage::Boards()) {
    CHECK(readme.find(std::string(board.file) + ".png  (" + std::to_string(board.width) + " x " +
                      std::to_string(board.height) + ")") != std::string::npos);
  }
  CHECK(readme.find("GAMEPLAY > TRACK SIGNAGE") != std::string::npos);
  CHECK(readme.find("--fm4_track_signage=false") != std::string::npos);
}
