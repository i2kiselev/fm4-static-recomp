// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "custom_decals.h"
#include "test_game_root.h"
#include "ui/hostui/ui_archive.h"

namespace decals = fm4_static_recomp::decals;

namespace {

constexpr size_t kWidthAt = 0x13F;  // the texture header: width, height, levels

uint32_t Get32(const std::vector<uint8_t>& v, size_t at) {
  return uint32_t(v[at]) << 24 | uint32_t(v[at + 1]) << 16 | uint32_t(v[at + 2]) << 8 | v[at + 3];
}

// A blank game decal: a mesh, the texture header (256 x 256, 9 levels), a 00000001 marker before
// every level, the material name in the trailer.
std::vector<uint8_t> BlankDecal() {
  std::vector<uint8_t> file(decals::kDecalFileSize, 0);
  for (size_t i = 0; i < kWidthAt; ++i) file[i] = uint8_t(i * 31 + 7);
  for (uint32_t k = 0; k < 3; ++k) file[kWidthAt + 4 * k + 2] = k < 2 ? 1 : 0;  // 0x100, 0x100
  file[kWidthAt + 11] = 9;
  size_t at = decals::kDecalPixelsAt;
  for (int w = 256; w >= 1; w /= 2) {
    file[at - 1] = 1;
    at += size_t(w) * w * 4 + 4;
  }
  std::memcpy(&file[file.size() - 120], "01___Default", 12);
  return file;
}

std::vector<uint8_t> Pattern(int size) {
  std::vector<uint8_t> rgba(size_t(size) * size * 4);
  for (size_t i = 0; i < rgba.size(); ++i) rgba[i] = uint8_t((i * 7919u) >> 3);
  return rgba;
}

void Put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s));
}

// An LSB2 string table with these keys (and no texts).
std::vector<uint8_t> Table(std::initializer_list<uint16_t> keys) {
  std::vector<uint8_t> t = {'L', 'S', 'B', '2', 0, 0, 0, 1, 0, 0, 0, 0};
  for (uint32_t w : {0u, 0u, 0x24u, 0u, 0u, 0u}) Put32(t, w);
  Put32(t, uint32_t(8 + keys.size() * 6));
  Put32(t, uint32_t(keys.size()));
  for (uint16_t key : keys) {
    t.push_back(uint8_t(key >> 8));
    t.push_back(uint8_t(key));
    Put32(t, 0);
  }
  return t;
}

bool Contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

}  // namespace

TEST_CASE("A decal file keeps the template's mesh and decodes to its image", "[custom_decals]") {
  const auto templ = BlankDecal();
  REQUIRE(decals::IsDecalFile(templ));
  CHECK_FALSE(decals::IsDecalFile(std::vector<uint8_t>(100)));
  CHECK(decals::DecalFileSize(256) == decals::kDecalFileSize);
  for (int size : {256, decals::kCustomDecalSize}) {
    const auto image = Pattern(size);
    const auto file = decals::EncodeDecal(image, size, templ);
    REQUIRE(file.size() == decals::DecalFileSize(size));
    int decoded_size = 0;
    CHECK(decals::DecodeDecal(file, &decoded_size) == image);
    CHECK(decoded_size == size);
    // The mesh and trailer are the template's; the header has this size and level count.
    CHECK(std::equal(file.begin(), file.begin() + kWidthAt, templ.begin()));
    CHECK(Get32(file, kWidthAt) == uint32_t(size));
    CHECK(Get32(file, kWidthAt + 4) == uint32_t(size));
    CHECK(Get32(file, kWidthAt + 8) == (size == 256 ? 9u : 10u));
    CHECK(std::equal(file.end() - 180, file.end(), templ.end() - 180));
    // A 00000001 marker before every level.
    size_t at = decals::kDecalPixelsAt;
    for (int w = size; w >= 1; w /= 2) {
      CHECK(Get32(file, at - 4) == 1u);
      at += size_t(w) * w * 4 + 4;
    }
  }
  // At 256 the file has a game decal's layout.
  CHECK(decals::IsDecalFile(decals::EncodeDecal(Pattern(256), 256, templ)));
  CHECK(decals::DecalFileSize(512) == 1398655);
  CHECK_FALSE(decals::DecodeDecal(std::vector<uint8_t>(100)));
}

TEST_CASE("Images fit the decal square in proportion on transparency", "[custom_decals]") {
  std::vector<uint8_t> wide(512 * 128 * 4);
  for (size_t i = 0; i < wide.size(); i += 4) {
    wide[i] = 200;
    wide[i + 3] = 255;
  }
  const auto fit = decals::FitImage(wide, 512, 128, 256);
  const auto alpha = [&](int x, int y) { return fit[(size_t(y) * 256 + x) * 4 + 3]; };
  CHECK(alpha(128, 128) == 255);
  CHECK(fit[(size_t(128) * 256 + 128) * 4] == 200);
  CHECK(alpha(0, 96) == 255);
  CHECK(alpha(255, 159) == 255);
  CHECK(alpha(128, 95) == 0);
  CHECK(alpha(128, 160) == 0);
  CHECK(decals::FitImage({}, 0, 0, 256) == std::vector<uint8_t>(256 * 256 * 4, 0));
}

TEST_CASE("Small images are enlarged smoothly, colours kept at transparent edges", "[custom_decals]") {
  // 2 x 1: opaque black, opaque white -> enlarged, the middle is grey, not a hard step.
  const std::vector<uint8_t> two = {0, 0, 0, 255, 255, 255, 255, 255};
  const auto fit = decals::FitImage(two, 2, 1, 64);
  const auto red = [&](int x, int y) { return int(fit[(size_t(y) * 64 + x) * 4]); };
  CHECK(red(2, 32) == 0);
  CHECK(red(61, 32) == 255);
  CHECK(red(31, 32) > 100);
  CHECK(red(31, 32) < 155);
  // Opaque red beside transparent black: the blend stays red, only the alpha falls.
  const std::vector<uint8_t> edge = {255, 0, 0, 255, 0, 0, 0, 0};
  const auto faded = decals::FitImage(edge, 2, 1, 64);
  const uint8_t* mid = &faded[(size_t(32) * 64 + 31) * 4];
  CHECK(mid[0] == 255);
  CHECK(mid[1] == 0);
  CHECK(mid[3] > 0);
  CHECK(mid[3] < 255);
}

TEST_CASE("Decal ids are kept for good and never handed out twice", "[custom_decals]") {
  auto ids = decals::AssignIds({"b", "a"}, {});
  CHECK(ids == std::map<std::string, int>{{"a", 9002}, {"b", 9001}});
  // Removing an image keeps its id; a new one gets the next id never used.
  ids = decals::AssignIds({"a", "c"}, ids);
  CHECK(ids.at("a") == 9002);
  CHECK(ids.at("b") == 9001);
  CHECK(ids.at("c") == 9003);
  CHECK(decals::ParseIds(decals::FormatIds(ids)) == ids);
  CHECK(decals::ParseIds("# note\n9005\tname with spaces\r\nbad\n12\ttoo low\n") ==
        std::map<std::string, int>{{"name with spaces", 9005}});
  std::vector<std::string> many;
  for (int i = 0; i < decals::kMaxDecals + 5; ++i) many.push_back("n" + std::to_string(i));
  CHECK(decals::AssignIds(many, {}).size() == size_t(decals::kMaxDecals));
}

TEST_CASE("Name keys miss the game's own decal names", "[custom_decals]") {
  REQUIRE(decals::StringHashes(Table({})) == std::set<uint16_t>{});
  CHECK(decals::StringHashes(Table({0x1234, 0x0042})) == std::set<uint16_t>{0x0042, 0x1234});
  CHECK_FALSE(decals::StringHashes({'n', 'o'}));
  const auto keys = decals::AssignKeys({9003, 9001, 9002}, {0xF4A0, 0xF4A2});
  CHECK(keys == std::map<int, uint16_t>{{9001, 0xF4A1}, {9002, 0xF4A3}, {9003, 0xF4A4}});
}

TEST_CASE("Decal files are named by id and image, so an edited image is a new file", "[custom_decals]") {
  const std::vector<uint8_t> a = {1, 2, 3}, b = {1, 2, 4};
  CHECK(decals::ImageHash(a) == decals::ImageHash(a));
  CHECK(decals::ImageHash(a) != decals::ImageHash(b));
  CHECK(decals::DecalFileName(9001, 0x0123456789ABCDEFull) == "custom_9001_0123456789abcdef.carbin");
}

TEST_CASE("The database statements replace the custom rows", "[custom_decals]") {
  const auto none = decals::DecalStatements({}, false);
  REQUIRE(none.size() == 2);  // the old rows go, nothing comes
  CHECK(none[0] == "DELETE FROM Livery_DecalsSortOrder WHERE ID >= 90000000");
  CHECK(none[1] == "DELETE FROM Livery_Decals WHERE ID BETWEEN 9001 AND 9999");

  const auto two = decals::DecalStatements({{9001, "Team's Logo", "custom_9001_00000000000000aa.carbin", 0xF4A1},
                                            {9002, "Stripe", "custom_9002_00000000000000bb.carbin", 0xF4A3}},
                                           true);
  REQUIRE(two.size() == 5);  // the custom decals are on no car's list: their own menu items list them
  CHECK(two[2] ==
        "INSERT INTO Livery_Decals (ID, Filename, DisplayName, AlphaSort) VALUES (9001, "
        "'GAME:\\Media\\Livery\\Decals\\custom_9001_00000000000000aa.carbin', '_&2939286689', 'Team''s Logo')");
  CHECK(Contains(two[3], "(9002, 'GAME:\\Media\\Livery\\Decals\\custom_9002_00000000000000bb.carbin'"));
  // Every listless game decal, after each maker's usual ones.
  CHECK(Contains(two[4], "INSERT INTO Livery_DecalsSortOrder"));
  CHECK(Contains(two[4], "m.MakeID, 900, d.ID"));
  CHECK(Contains(two[4], "d.ID < 9001 AND d.ID NOT IN (SELECT Livery_DecalID FROM Livery_DecalsSortOrder WHERE ID < 90000000)"));
  // The sponsors alone need no decal rows.
  const auto sponsors = decals::DecalStatements({}, true);
  REQUIRE(sponsors.size() == 3);
  CHECK(Contains(sponsors[2], "m.MakeID, 900, d.ID"));
}

TEST_CASE("A custom decal item's picker lists the custom decals alone", "[custom_decals]") {
  // The picker's queries, as the game sends them.
  CHECK(decals::CustomPickerQuery(
            "SELECT Livery_Decals.ID AS ID FROM Livery_Decals INNER JOIN Livery_DecalsSortOrder ON "
            "(Livery_Decals.ID = Livery_DecalsSortOrder.Livery_DecalID) WHERE MakeID = 52 ORDER BY Sequence, "
            "AlphaSort") ==
        "SELECT Livery_Decals.ID AS ID FROM Livery_Decals WHERE Livery_Decals.ID BETWEEN 9001 AND 9999 ORDER BY "
        "Livery_Decals.AlphaSort");
  CHECK(decals::CustomPickerQuery(
            "SELECT Livery_Decals.FileName FROM Livery_Decals  INNER JOIN Livery_DecalsSortOrder ON "
            "(Livery_Decals.ID = Livery_DecalsSortOrder.Livery_DecalID) WHERE MakeID = 7 ORDER BY Sequence, AlphaSort") ==
        "SELECT Livery_Decals.FileName FROM Livery_Decals  WHERE Livery_Decals.ID BETWEEN 9001 AND 9999 ORDER BY "
        "Livery_Decals.AlphaSort");
  // Anything else is left alone.
  CHECK_FALSE(decals::CustomPickerQuery("SELECT ID, Filename AS elemname FROM Livery_Decals ORDER BY ID"));
  CHECK_FALSE(decals::CustomPickerQuery("DELETE FROM Livery_DecalsSortOrder INNER JOIN Livery_DecalsSortOrder ON "
                                        "(Livery_Decals.ID = Livery_DecalsSortOrder.Livery_DecalID)"));
  decals::SetCustomPicker(true);
  CHECK(decals::CustomPicker());
  decals::SetCustomPicker(false);
  CHECK_FALSE(decals::CustomPicker());
}

TEST_CASE("Published decals reach the hooks once per change", "[custom_decals]") {
  uint64_t seen = 0;
  std::vector<std::string> statements;
  decals::StatementsSince(seen, nullptr);  // catch up with whatever an earlier test published
  CHECK_FALSE(decals::StatementsSince(seen, &statements));
  decals::Publish({"A", "B"}, {{0xF4A1, u"Caf\u00e9"}}, {false, 1, {}});
  REQUIRE(decals::StatementsSince(seen, &statements));
  CHECK(statements == std::vector<std::string>{"A", "B"});
  CHECK_FALSE(decals::StatementsSince(seen, &statements));
  CHECK(decals::DecalName(0xF4A1) == std::u16string(u"Caf\u00e9"));
  CHECK_FALSE(decals::DecalName(0xF4A2));
  CHECK(decals::CurrentStatus().decals == 1);
  decals::SetBuilding();
  CHECK(decals::CurrentStatus().building);
  decals::Publish({}, {}, {false, 0, {"x.png (not a PNG image the game can read)"}});
  CHECK_FALSE(decals::DecalName(0xF4A1));  // a name goes with its decal
  CHECK_FALSE(decals::CurrentStatus().building);
  CHECK(decals::CurrentStatus().problems.size() == 1);
  CHECK(decals::StatementsSince(seen, &statements));
  CHECK(statements.empty());
}

TEST_CASE("The menu items say to reopen the editor while new decals wait", "[custom_decals]") {
  uint64_t seen = 0;
  decals::StatementsSince(seen, nullptr);
  decals::Status status{false, 1, {}, 0xF4A0, 0xF4A1};
  decals::Publish({"A"}, {{0xF4A0, u"APPLY A CUSTOM DECAL"}, {0xF4A1, u"CHANGE CUSTOM DECAL"}, {0xF4A2, u"Logo"}},
                  status);
  // Made, not yet taken by the game (it takes them as the paint editor opens).
  CHECK(decals::ChangesWaiting());
  CHECK(decals::DecalName(0xF4A0) == std::u16string(u"APPLY A CUSTOM DECAL (REOPEN EDITOR TO UPDATE)"));
  CHECK(decals::DecalName(0xF4A1) == std::u16string(u"CHANGE CUSTOM DECAL (REOPEN EDITOR TO UPDATE)"));
  CHECK(decals::DecalName(0xF4A2) == std::u16string(u"Logo"));  // decal names never change
  REQUIRE(decals::StatementsSince(seen, nullptr));
  CHECK_FALSE(decals::ChangesWaiting());
  CHECK(decals::DecalName(0xF4A0) == std::u16string(u"APPLY A CUSTOM DECAL"));
  decals::SetBuilding();
  CHECK(decals::ChangesWaiting());
  CHECK(decals::DecalName(0xF4A1) == std::u16string(u"CHANGE CUSTOM DECAL (REOPEN EDITOR TO UPDATE)"));
  decals::Publish({}, {}, {});
  decals::StatementsSince(seen, nullptr);
}

TEST_CASE("A game decal is the template and its names keep their keys", "[custom_decals]") {
  const auto livery = fm4_static_recomp::hostui::UiArchive::Open(TestGameRoot() / "Media/Livery.zip");
  const auto strings = fm4_static_recomp::hostui::UiArchive::Open(TestGameRoot() / "Media/StringTables/EN.zip");
  if (!livery || !strings) {
    SKIP("Media/Livery.zip or Media/StringTables/EN.zip not found under " << TestGameRoot().string());
  }
  const auto templ = livery->Read("decals/decal002.carbin");
  REQUIRE(templ);
  REQUIRE(decals::IsDecalFile(*templ));
  // A game decal comes back unchanged through decode and encode at full size.
  const auto image = decals::DecodeDecal(*templ);
  REQUIRE(image);
  const auto again = decals::EncodeDecal(*image, 256, *templ);
  CHECK(std::equal(again.begin(), again.begin() + decals::kDecalPixelsAt + 256 * 256 * 4, templ->begin()));
  CHECK(decals::EncodeDecal(decals::FitImage(*image, 256, 256, 512), 512, *templ).size() == 1398655);
  const auto table = strings->Read("Livery_Decals.str");
  REQUIRE(table);
  const auto hashes = decals::StringHashes(*table);
  REQUIRE(hashes);
  CHECK(hashes->size() > 100);
  // The keys custom decals get are none of the game's.
  for (const auto& [id, key] : decals::AssignKeys({9001, 9002, 9003}, *hashes)) CHECK_FALSE(hashes->contains(key));
}
