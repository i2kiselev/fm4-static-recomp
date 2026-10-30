#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "input/controls.h"
#include "input/prompt_strings.h"
#include "ui/hostui/ui_archive.h"
#include "test_game_root.h"

using fm4_static_recomp::hostui::UiArchive;
using namespace fm4_static_recomp::input;

namespace {

// game_hooks.cpp's GRAPHICS entry in My Profile -> Options, a key of its own.
constexpr uint16_t kGraphicsStringKey = 0x7158;

struct StringEntry {
  std::string name;  // IDS_...
  std::u16string text;
};

uint32_t Big32(std::span<const uint8_t> data, size_t at) {
  return uint32_t(data[at]) << 24 | uint32_t(data[at + 1]) << 16 | uint32_t(data[at + 2]) << 8 |
         data[at + 3];
}

uint16_t Big16(std::span<const uint8_t> data, size_t at) {
  return uint16_t(data[at] << 8 | data[at + 1]);
}

// An LSB2 string table (.str): at +0x0C the texts' chunk, [size][count], (u16 key, u32 char
// offset) x count + a sentinel, then a UTF-16BE pool; at +0x18 the names' chunk, [size][count],
// (u16 key, u16 index, u32 byte offset) x count, then an ASCII pool.
std::optional<std::map<uint16_t, StringEntry>> ParseStringTable(std::span<const uint8_t> data) {
  if (data.size() < 0x1C || std::string_view(reinterpret_cast<const char*>(data.data()), 4) != "LSB2") {
    return std::nullopt;
  }
  std::map<uint16_t, StringEntry> entries;
  const size_t texts = Big32(data, 0x0C);
  const uint32_t count = Big32(data, texts + 4);
  const size_t pool = texts + 8 + 6 * (size_t(count) + 1);
  if (pool > data.size()) {
    return std::nullopt;
  }
  for (uint32_t i = 0; i < count; ++i) {
    const size_t entry = texts + 8 + 6 * size_t(i);
    StringEntry& out = entries[Big16(data, entry)];
    for (size_t at = pool + 2 * size_t(Big32(data, entry + 2)); at + 1 < data.size() && Big16(data, at);
         at += 2) {
      out.text += char16_t(Big16(data, at));
    }
  }
  const size_t names = Big32(data, 0x18);
  if (names && names + 8 <= data.size() && Big32(data, names + 4) == count) {
    const size_t name_pool = names + 8 + 8 * size_t(count);
    for (uint32_t i = 0; i < count; ++i) {
      const size_t entry = names + 8 + 8 * size_t(i);
      for (size_t at = name_pool + Big32(data, entry + 4); at < data.size() && data[at]; ++at) {
        entries[Big16(data, entry)].name += char(data[at]);
      }
    }
  }
  return entries;
}

std::vector<std::filesystem::path> StringTableArchives() {
  std::vector<std::filesystem::path> archives;
  const auto folder = TestGameRoot() / "Media/StringTables";
  if (std::filesystem::is_directory(folder)) {
    for (const auto& file : std::filesystem::directory_iterator(folder)) {
      if (file.path().extension() == ".zip") {
        archives.push_back(file.path());
      }
    }
  }
  return archives;
}

}  // namespace

TEST_CASE("No string table on the disc holds the GRAPHICS entry's key", "[string_tables]") {
  const auto archives = StringTableArchives();
  if (archives.empty()) {
    SKIP("Media/StringTables not found under " << TestGameRoot().string());
  }
  size_t tables = 0;
  for (const auto& path : archives) {
    const auto archive = UiArchive::Open(path);
    INFO(path.string());
    REQUIRE(archive);
    for (const std::string& member : archive->MemberNames()) {
      if (!member.ends_with(".str")) {
        continue;
      }
      INFO(member);
      const auto data = archive->Read(member);
      // UiArchive's XMem decoding fails on this 500-byte member on every disc; its table is
      // checked in the other languages, which share its keys.
      if (!data && path.filename() == "HU.zip" && member == "i_hud_all.str") {
        continue;
      }
      REQUIRE(data);
      // DEV.zip, the developers' language, carries an empty livery_vinylsdecals.str.
      if (data->empty()) {
        continue;
      }
      const auto table = ParseStringTable(*data);
      REQUIRE(table);
      CHECK_FALSE(table->contains(kGraphicsStringKey));
      ++tables;
    }
  }
  // 18 languages, not all with every table: 3,288 on the 1.0 disc.
  CHECK(tables > 3000);
}

TEST_CASE("The prompt strings' keys name the same English entries, which name buttons",
          "[string_tables]") {
  struct Expected {
    uint16_t key;
    const char* table;
    const char* name;
  };
  // prompt_strings.cpp's list.
  constexpr Expected kExpected[] = {
      {0x3D74, "carexperience.str", "IDS_Explore_Description"},
      {0x42FB, "carexperience.str", "IDS_Interact_Description1"},
      {0x437B, "carexperience.str", "IDS_Interact_Description2"},
      {0xBA58, "carexperience.str", "IDS_Getin_Description"},
      {0xDE8C, "carexperience.str", "IDS_Move_Description2"},
      {0xDF0C, "carexperience.str", "IDS_Move_Description1"},
      {0xEC34, "carexperience.str", "IDS_Look_Description2"},
      {0xEDB4, "carexperience.str", "IDS_Look_Description1"},
      {0x4D92, "ingame.str", "IDS_Accelerate_Message"},
      {0x76B3, "ingame.str", "IDS_View_Message"},
      {0xF762, "ingame.str", "IDS_Steering_Message"},
      {0xFC27, "lobby.str", "IDS_OptionBucketNameDesc"},
      {0xC0F5, "main.str", "IDS_Rivals_Message"},
      {0x90AB, "mainmenu.str", "IDS_A_Start"},
      {0x206F, "myprofile.str", "IDS_Rewind_Message"},
      {0x6877, "myprofile.str", "IDS_Controller_Discription"},
      {0x63B4, "storefront.str", "IDS_GamertagDefault"},
      {0xE49E, "storefront.str", "IDS_ManualKeywordDefault"},
      {0x2ADE, "telemetry.str", "IDS_Press_Dpad_Down"},
      {0xD422, "tuning.str", "IDS_SetupFileLocked_Remove_Msg"},
      {0xEC3B, "upgradeareas.str", "IDS_Description_3"},
      {0xED3B, "upgradeareas.str", "IDS_Description_1"},
  };
  const auto archive = UiArchive::Open(TestGameRoot() / "Media/StringTables/EN.zip");
  if (!archive) {
    SKIP("Media/StringTables/EN.zip not found under " << TestGameRoot().string());
  }
  CHECK(PromptStringKeys().size() == std::size(kExpected));
  const Controls controls = DefaultControls();
  for (const Expected& expected : kExpected) {
    INFO(expected.table << " " << expected.name);
    CHECK(IsPromptStringKey(expected.key));
    const auto data = archive->Read(expected.table);
    REQUIRE(data);
    const auto table = ParseStringTable(*data);
    REQUIRE(table);
    const auto entry = table->find(expected.key);
    REQUIRE(entry != table->end());
    CHECK(entry->second.name == expected.name);
    CHECK(KeyboardText(expected.key, entry->second.text, controls));
  }
}
