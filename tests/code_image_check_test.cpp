#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "code_image_check.h"

using fm4_static_recomp::CodeImageMismatch;
using fm4_static_recomp::DiscRelease;
using fm4_static_recomp::FileSha256;

namespace {

constexpr DiscRelease kReleases[] = {
    {"1.0 PAL (Europe, Australia)", "1.0", "AA"},
    {"1.0 USA", "1.0", "BB"},
    {"Rev 1 Europe", "rev1", "CC"},
    {"Rev 1 USA", "rev1", "DD"},
};

}  // namespace

TEST_CASE("A release of the code image the build was made from starts") {
  CHECK_FALSE(CodeImageMismatch("AA", "1.0", kReleases));
  CHECK_FALSE(CodeImageMismatch("BB", "1.0", kReleases));
  CHECK_FALSE(CodeImageMismatch("dd", "rev1", kReleases));
}

TEST_CASE("A release of the other code image is refused, naming both") {
  const auto message = CodeImageMismatch("CC", "1.0", kReleases);
  REQUIRE(message);
  CHECK(message->find("1.0 PAL (Europe, Australia) or 1.0 USA") != std::string::npos);
  CHECK(message->find("Rev 1 Europe") != std::string::npos);
  CHECK(message->find("Rev 1 USA") == std::string::npos);
  CHECK(message->find("setup") != std::string::npos);

  const auto back = CodeImageMismatch("BB", "rev1", kReleases);
  REQUIRE(back);
  CHECK(back->find("Rev 1 Europe or Rev 1 USA") != std::string::npos);
  CHECK(back->find("1.0 USA") != std::string::npos);
}

TEST_CASE("An unknown or unreadable default.xex is refused") {
  const auto unknown = CodeImageMismatch("EE", "1.0", kReleases);
  REQUIRE(unknown);
  CHECK(unknown->find("EE") != std::string::npos);
  REQUIRE(CodeImageMismatch("", "1.0", kReleases));
}

TEST_CASE("FileSha256 hashes a file's bytes as upper-case hex") {
  const auto path = std::filesystem::temp_directory_path() / "fm4_code_image_check_test.bin";
  {
    std::ofstream out(path, std::ios::binary);
    out << "abc";
  }
  CHECK(FileSha256(path) == "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
  std::filesystem::remove(path);
  CHECK(FileSha256(path).empty());
}

TEST_CASE("The test disc's default.xex is a supported release") {
  // config/supported-discs.json's executables.
  constexpr DiscRelease kSupported[] = {
      {"1.0 PAL (Europe, Australia)", "1.0",
       "6A7E9AE561CCCA134916F27870A73299408F4AD14F1499063C0F6F6414C2A7E1"},
      {"1.0 USA", "1.0", "3618E85AF83E972DDCC1B0D0DD808E78E29AB4AB3890A15B31C210A6A3E93E09"},
      {"Rev 1 Europe", "rev1", "B34DFB88978819CCCDA6DAFD755E7139970A58FBF6707DFBCBA06846857F52BD"},
      {"Rev 1 USA", "rev1", "E0A93EE8A3E16DBB5C327B940EFB38E9D125809A4281AB75CBFA9CD987285D4C"},
  };
  const char* root = std::getenv("FM4_TEST_GAME_ROOT");
  const std::filesystem::path game = root ? root : FM4_TEST_GAME_ROOT;
  const std::string found = FileSha256(game / "default.xex");
  INFO(game.string() << " " << found);
  const bool as_1_0 = !CodeImageMismatch(found, "1.0", kSupported);
  const bool as_rev1 = !CodeImageMismatch(found, "rev1", kSupported);
  CHECK(as_1_0 != as_rev1);
}
