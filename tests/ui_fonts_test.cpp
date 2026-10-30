#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ui/hostui/ui_archive.h"
#include "ui/hostui/ui_fonts.h"
#include "test_game_root.h"

using fm4_static_recomp::hostui::Crc32;
using fm4_static_recomp::hostui::ExportUiFonts;
using fm4_static_recomp::hostui::UiArchive;

namespace {

void Put16(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(uint8_t(value));
  out.push_back(uint8_t(value >> 8));
}

void Put32(std::vector<uint8_t>& out, uint32_t value) {
  Put16(out, value & 0xFFFF);
  Put16(out, value >> 16);
}

// A ZIP whose members are stored (method 0), as the reader accepts besides XMem.
std::vector<uint8_t> StoredZip(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& members) {
  std::vector<uint8_t> zip;
  std::vector<uint8_t> directory;
  for (const auto& [name, data] : members) {
    const uint32_t offset = uint32_t(zip.size());
    const uint32_t crc = Crc32(data);
    Put32(zip, 0x04034B50);
    Put16(zip, 20);
    Put16(zip, 0);
    Put16(zip, 0);
    Put32(zip, 0);
    Put32(zip, crc);
    Put32(zip, uint32_t(data.size()));
    Put32(zip, uint32_t(data.size()));
    Put16(zip, uint16_t(name.size()));
    Put16(zip, 0);
    zip.insert(zip.end(), name.begin(), name.end());
    zip.insert(zip.end(), data.begin(), data.end());
    Put32(directory, 0x02014B50);
    Put16(directory, 20);
    Put16(directory, 20);
    Put16(directory, 0);
    Put16(directory, 0);
    Put32(directory, 0);
    Put32(directory, crc);
    Put32(directory, uint32_t(data.size()));
    Put32(directory, uint32_t(data.size()));
    Put16(directory, uint16_t(name.size()));
    Put16(directory, 0);
    Put16(directory, 0);
    Put16(directory, 0);
    Put16(directory, 0);
    Put32(directory, 0);
    Put32(directory, offset);
    directory.insert(directory.end(), name.begin(), name.end());
  }
  const uint32_t directory_offset = uint32_t(zip.size());
  zip.insert(zip.end(), directory.begin(), directory.end());
  Put32(zip, 0x06054B50);
  Put16(zip, 0);
  Put16(zip, 0);
  Put16(zip, uint16_t(members.size()));
  Put16(zip, uint16_t(members.size()));
  Put32(zip, uint32_t(directory.size()));
  Put32(zip, directory_offset);
  Put16(zip, 0);
  return zip;
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

class TempDir {
 public:
  TempDir() : path_(std::filesystem::temp_directory_path() / "fm4_ui_fonts_test") {
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

std::filesystem::path WriteZip(const TempDir& dir,
                               const std::vector<std::pair<std::string, std::vector<uint8_t>>>& members) {
  const auto path = dir.path() / "Fonts.zip";
  const auto bytes = StoredZip(members);
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                              std::streamsize(bytes.size()));
  return path;
}

// The disc's own fonts, decompressed: the only valid fonts the test can use without shipping one.
std::optional<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> DiscFonts() {
  const auto archive = UiArchive::Open(TestGameRoot() / "Media/UI/Fonts.zip");
  if (!archive) {
    return std::nullopt;
  }
  auto a = archive->Read("a_vector_aa.dt");
  auto b = archive->Read("b_vector_aa.dt");
  if (!a || !b) {
    return std::nullopt;
  }
  return std::pair{std::move(*a), std::move(*b)};
}

#define REQUIRE_DISC_FONTS(fonts)                                  \
  const auto fonts = DiscFonts();                                  \
  if (!fonts) {                                                    \
    SKIP("Fonts.zip not found under " << TestGameRoot().string()); \
  }

}  // namespace

TEST_CASE("Both fonts are exported byte for byte", "[ui-fonts]") {
  REQUIRE_DISC_FONTS(fonts);
  TempDir dir;
  const auto zip = WriteZip(dir, {{"a_vector_aa.dt", fonts->first}, {"b_vector_aa.dt", fonts->second}});
  std::string error;
  REQUIRE(ExportUiFonts(zip, dir.path() / "out", &error));
  CHECK(error.empty());
  CHECK(ReadFile(dir.path() / "out" / "a_vector_aa.dt") == fonts->first);
  CHECK(ReadFile(dir.path() / "out" / "b_vector_aa.dt") == fonts->second);
}

TEST_CASE("A missing font member is named", "[ui-fonts]") {
  REQUIRE_DISC_FONTS(fonts);
  TempDir dir;
  const auto zip = WriteZip(dir, {{"a_vector_aa.dt", fonts->first}});
  std::string error;
  CHECK_FALSE(ExportUiFonts(zip, dir.path() / "out", &error));
  CHECK(error.find("b_vector_aa.dt") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(dir.path() / "out" / "b_vector_aa.dt"));
}

TEST_CASE("A member that is not a vector font is refused", "[ui-fonts]") {
  REQUIRE_DISC_FONTS(fonts);
  TempDir dir;
  const auto zip = WriteZip(dir, {{"a_vector_aa.dt", fonts->first},
                                  {"b_vector_aa.dt", std::vector<uint8_t>(64, 0xAB)}});
  std::string error;
  CHECK_FALSE(ExportUiFonts(zip, dir.path() / "out", &error));
  CHECK(error.find("b_vector_aa.dt") != std::string::npos);
}

TEST_CASE("A missing archive is reported", "[ui-fonts]") {
  TempDir dir;
  std::string error;
  CHECK_FALSE(ExportUiFonts(dir.path() / "absent.zip", dir.path() / "out", &error));
  CHECK(error.find("absent.zip") != std::string::npos);
}
