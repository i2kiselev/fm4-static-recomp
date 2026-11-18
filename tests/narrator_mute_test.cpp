// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "narrator_mute.h"
#include "test_game_root.h"

using fm4_static_recomp::MuteNarratorBank;

namespace {

template <typename T>
void Put(std::vector<uint8_t>& bytes, size_t at, T value) {
  std::memcpy(bytes.data() + at, &value, sizeof(value));
}

template <typename T>
T Get(const std::vector<uint8_t>& bytes, size_t at) {
  T value;
  std::memcpy(&value, bytes.data() + at, sizeof(value));
  return value;
}

// An FSB4 bank with sample headers of the given sizes, each at full volume, 4 padding bytes
// after the headers and a few bytes of audio data.
std::vector<uint8_t> Bank(std::vector<uint16_t> header_sizes) {
  uint32_t headers = 4;
  for (const uint16_t size : header_sizes) headers += size;
  std::vector<uint8_t> bank(0x30 + headers + 64, 0xAB);
  std::memcpy(bank.data(), "FSB4", 4);
  Put<uint32_t>(bank, 4, static_cast<uint32_t>(header_sizes.size()));
  Put<uint32_t>(bank, 8, headers);
  Put<uint32_t>(bank, 12, 64);
  size_t at = 0x30;
  for (const uint16_t size : header_sizes) {
    Put<uint16_t>(bank, at, size);
    Put<uint32_t>(bank, at + 32, 90000);
    Put<uint32_t>(bank, at + 40, 0);
    Put<uint32_t>(bank, at + 44, 89999);
    Put<uint16_t>(bank, at + 56, 255);
    at += size;
  }
  return bank;
}

}  // namespace

TEST_CASE("Every clip is silenced and shortened, the size and data stay", "[narrator]") {
  const auto original = Bank({116, 112, 124});
  auto bank = original;
  REQUIRE(MuteNarratorBank(bank));
  CHECK(bank.size() == original.size());
  size_t at = 0x30;
  for (const uint16_t size : {116, 112, 124}) {
    CHECK(Get<uint16_t>(bank, at + 56) == 0);
    CHECK(Get<uint32_t>(bank, at + 32) == 512);
    CHECK(Get<uint32_t>(bank, at + 40) == 0);
    CHECK(Get<uint32_t>(bank, at + 44) == 511);
    at += size;
  }
  CHECK(std::equal(bank.begin() + at, bank.end(), original.begin() + at));
}

TEST_CASE("A bank that is not the disc's is left alone", "[narrator]") {
  auto muted = Bank({116, 112});
  REQUIRE(MuteNarratorBank(muted));
  const auto again = muted;
  CHECK_FALSE(MuteNarratorBank(muted));
  CHECK(muted == again);

  auto quiet = Bank({116, 112});
  Put<uint16_t>(quiet, 0x30 + 116 + 56, 128);
  const auto quiet_before = quiet;
  CHECK_FALSE(MuteNarratorBank(quiet));
  CHECK(quiet == quiet_before);

  auto fsb3 = Bank({116});
  std::memcpy(fsb3.data(), "FSB3", 4);
  CHECK_FALSE(MuteNarratorBank(fsb3));

  auto overrun = Bank({116, 112});
  Put<uint32_t>(overrun, 4, 3);
  CHECK_FALSE(MuteNarratorBank(overrun));

  std::vector<uint8_t> tiny = {'F', 'S', 'B', '4'};
  CHECK_FALSE(MuteNarratorBank(tiny));
}

TEST_CASE("The disc's voice banks are all muted at their own size", "[narrator][disc]") {
  const auto vo = TestGameRoot() / "Media/audio/VO";
  if (!std::filesystem::is_directory(vo)) {
    SKIP("Media/audio/VO not found under " << TestGameRoot().string());
  }
  int banks = 0;
  for (const auto& entry : std::filesystem::directory_iterator(vo)) {
    const auto path = entry.path() / "vo.fsb";
    if (!std::filesystem::is_regular_file(path)) continue;
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> bank((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const size_t size = bank.size();
    INFO(path.string());
    CHECK(MuteNarratorBank(bank));
    CHECK(bank.size() == size);
    ++banks;
  }
  CHECK(banks > 0);
}
