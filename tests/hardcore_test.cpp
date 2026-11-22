// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <rex/cvar.h>

#include <sqlite3.h>

#include "hardcore.h"

namespace {

using fm4_static_recomp::ActiveHardcoreOptions;
using fm4_static_recomp::EditDatabase;
using fm4_static_recomp::FormatToFit;
using fm4_static_recomp::GetTunable;
using fm4_static_recomp::RacePayoutEdits;
using fm4_static_recomp::RepairCostEdits;
using fm4_static_recomp::ResaleValueEdits;
using fm4_static_recomp::ScaleTunable;
using fm4_static_recomp::SetTunable;
using fm4_static_recomp::TunableScale;

// The game's tunables these options scale, as PFP4 1.3 and the disc have them.
constexpr const char* kPfp4Tunables =
    "CarValueScales\\SellValueScale 0.75\r\nCarValueScales\\RestockingValueScale 0.75\r\n"
    "RaceWinnings\\CreditsPerMile 1000\r\nRaceWinnings\\DmgPenaltyBody 0.35\r\n"
    "RaceWinnings\\DmgPenaltyEngine 0.3\r\n";
constexpr const char* kDiscTunables =
    "CarValueScales\\SellValueScale 0.5\r\nCarValueScales\\RestockingValueScale 0.5\r\n"
    "RaceWinnings\\CreditsPerMile 1500\r\n";

// `text` with every scale of `scales` made, or nullopt.
std::optional<std::string> Scale(std::string text, const std::vector<TunableScale>& scales) {
  for (const TunableScale& scale : scales) {
    auto next = ScaleTunable(text, scale);
    if (!next) return std::nullopt;
    text = std::move(*next);
  }
  return text;
}

// A database made by `sql`, as the bytes of its file.
std::vector<uint8_t> MakeDatabase(const char* sql) {
  sqlite3* db = nullptr;
  REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
  REQUIRE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_int64 size = 0;
  unsigned char* bytes = sqlite3_serialize(db, "main", &size, 0);
  REQUIRE(bytes != nullptr);
  std::vector<uint8_t> result(bytes, bytes + size);
  sqlite3_free(bytes);
  sqlite3_close(db);
  return result;
}

// The first column of each row `sql` returns from the database `bytes`, as text.
std::vector<std::string> Query(const std::vector<uint8_t>& bytes, const char* sql) {
  sqlite3* db = nullptr;
  REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
  auto* copy = static_cast<unsigned char*>(sqlite3_malloc64(bytes.size()));
  std::memcpy(copy, bytes.data(), bytes.size());
  REQUIRE(sqlite3_deserialize(db, "main", copy, sqlite3_int64(bytes.size()),
                              sqlite3_int64(bytes.size()), SQLITE_DESERIALIZE_FREEONCLOSE) == SQLITE_OK);
  std::vector<std::string> rows;
  sqlite3_stmt* statement = nullptr;
  REQUIRE(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK);
  while (sqlite3_step(statement) == SQLITE_ROW) {
    rows.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)));
  }
  sqlite3_finalize(statement);
  sqlite3_close(db);
  return rows;
}

}  // namespace

TEST_CASE("SetTunable sets a value and keeps the text's length", "[hardcore]") {
  const std::string text = "RaceWinnings\\CreditsPerMile 1000\r\nRaceWinnings\\MaxBaseCreditsPerRace 500000\r\n";
  const auto result = SetTunable(text, "RaceWinnings\\CreditsPerMile", "500");
  REQUIRE(result);
  CHECK(result->size() == text.size());
  CHECK(*result ==
        "RaceWinnings\\CreditsPerMile 500 \r\nRaceWinnings\\MaxBaseCreditsPerRace 500000\r\n");
}

TEST_CASE("SetTunable matches the whole key on a line of its own", "[hardcore]") {
  // A key that only starts another key is not that key.
  const std::string text = "A\\Bound 20\nA\\BoundMax 30\n";
  CHECK(SetTunable(text, "A\\Bound", "1") == std::optional<std::string>("A\\Bound 1 \nA\\BoundMax 30\n"));
  CHECK(SetTunable(text, "A\\BoundMax", "9") == std::optional<std::string>("A\\Bound 20\nA\\BoundMax 9 \n"));
  // The last line, without a line break.
  CHECK(SetTunable("A\\X 1", "A\\X", "2") == std::optional<std::string>("A\\X 2"));
}

TEST_CASE("SetTunable refuses a missing key, a doubled key and a value that does not fit",
          "[hardcore]") {
  CHECK_FALSE(SetTunable("A\\X 1\n", "A\\Y", "2"));
  CHECK_FALSE(SetTunable("A\\X 1\nA\\X 2\n", "A\\X", "3"));
  CHECK_FALSE(SetTunable("A\\X 1\n", "A\\X", "100"));
  CHECK(SetTunable("A\\X 1  \n", "A\\X", "100") == std::optional<std::string>("A\\X 100\n"));
}

TEST_CASE("EditDatabase runs the statements on a copy of the same size", "[hardcore]") {
  const auto db = MakeDatabase(
      "CREATE TABLE Scoring (place INTEGER, credits INTEGER);"
      "INSERT INTO Scoring VALUES (1, 1000), (2, 700), (3, 500);");
  std::string error;
  const auto edited = EditDatabase(db, {"UPDATE Scoring SET credits = credits / 2"}, &error);
  REQUIRE(edited);
  CHECK(error.empty());
  CHECK(edited->size() == db.size());
  CHECK(Query(*edited, "SELECT credits FROM Scoring ORDER BY place") ==
        std::vector<std::string>{"500", "350", "250"});
  // The original is untouched.
  CHECK(Query(db, "SELECT credits FROM Scoring ORDER BY place") ==
        std::vector<std::string>{"1000", "700", "500"});
}

TEST_CASE("EditDatabase refuses bad statements and what is not a database", "[hardcore]") {
  const auto db = MakeDatabase("CREATE TABLE T (x INTEGER); INSERT INTO T VALUES (1);");
  std::string error;
  CHECK_FALSE(EditDatabase(db, {"UPDATE Missing SET x = 2"}, &error));
  CHECK(error.find("Missing") != std::string::npos);
  error.clear();
  CHECK_FALSE(EditDatabase(std::vector<uint8_t>(4096, 0x5A), {"UPDATE T SET x = 2"}, &error));
  CHECK_FALSE(error.empty());
  CHECK_FALSE(EditDatabase({}, {"UPDATE T SET x = 2"}, &error));
}

TEST_CASE("EditDatabase refuses a result of another size", "[hardcore]") {
  // Filling a table past its page grows the file: an override must keep the file's size.
  const auto db = MakeDatabase("CREATE TABLE T (x TEXT);");
  std::string error;
  CHECK_FALSE(EditDatabase(db, {"INSERT INTO T SELECT hex(randomblob(2000)) FROM "
                                "(WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n "
                                "WHERE i < 20) SELECT i FROM n)"},
                           &error));
  CHECK(error.find("changed size") != std::string::npos);
}

TEST_CASE("GetTunable reads a value as written", "[hardcore]") {
  CHECK(GetTunable(kPfp4Tunables, "CarValueScales\\SellValueScale") == std::optional<std::string>("0.75"));
  CHECK(GetTunable("A\\X 50.0 \n", "A\\X") == std::optional<std::string>("50.0"));
  CHECK_FALSE(GetTunable(kPfp4Tunables, "CarValueScales\\Sell"));
}

TEST_CASE("FormatToFit rounds to what fits, between two to the game's own", "[hardcore]") {
  CHECK(FormatToFit(0.5625, 4, false, 0.75) == std::optional<std::string>("0.56"));
  CHECK(FormatToFit(0.375, 3, false, 0.5) == std::optional<std::string>("0.4"));
  CHECK(FormatToFit(0.25, 3, false, 0.5) == std::optional<std::string>("0.3"));  // halfway
  CHECK(FormatToFit(750.0, 4, true, 1000) == std::optional<std::string>("750"));
  CHECK(FormatToFit(1.0, 4, false, 0.35) == std::optional<std::string>("1"));
  CHECK_FALSE(FormatToFit(12345.0, 4, true, 1000));
  CHECK_FALSE(FormatToFit(-1.0, 4, false, 1));
}

TEST_CASE("RESALE VALUE pays less, in the file's own digits", "[hardcore]") {
  CHECK(Scale(kPfp4Tunables, ResaleValueEdits(75).scales) ==
        std::optional<std::string>(
            "CarValueScales\\SellValueScale 0.56\r\nCarValueScales\\RestockingValueScale 0.56\r\n"
            "RaceWinnings\\CreditsPerMile 1000\r\nRaceWinnings\\DmgPenaltyBody 0.35\r\n"
            "RaceWinnings\\DmgPenaltyEngine 0.3\r\n"));
  const auto half = Scale(kPfp4Tunables, ResaleValueEdits(50).scales);
  REQUIRE(half);
  CHECK(GetTunable(*half, "CarValueScales\\SellValueScale") == std::optional<std::string>("0.38"));
  // The disc's "0.5" has one digit to give: about -20% and -40%.
  const auto disc = Scale(kDiscTunables, ResaleValueEdits(75).scales);
  REQUIRE(disc);
  CHECK(GetTunable(*disc, "CarValueScales\\SellValueScale") == std::optional<std::string>("0.4"));
  CHECK(disc->size() == std::string(kDiscTunables).size());
}

TEST_CASE("RACE PAYOUTS scale the credits by distance and the affinity bonuses",
          "[hardcore]") {
  const auto text = Scale(kPfp4Tunables, RacePayoutEdits(75).scales);
  REQUIRE(text);
  CHECK(GetTunable(*text, "RaceWinnings\\CreditsPerMile") == std::optional<std::string>("750"));
  const auto db = MakeDatabase(
      "CREATE TABLE AffinityLevelDef (Id INTEGER, XP INTEGER, Discount REAL, BonusCredits INTEGER);"
      "INSERT INTO AffinityLevelDef VALUES (1, 1875, 0.0, 500), (2, 6750, 0.0, 1000);");
  std::string error;
  const auto edited = EditDatabase(db, RacePayoutEdits(50).sql, &error);
  REQUIRE(edited);
  CHECK(Query(*edited, "SELECT BonusCredits || ',' || XP FROM AffinityLevelDef ORDER BY Id") ==
        std::vector<std::string>{"250,1875", "500,6750"});
}

TEST_CASE("REPAIR COSTS multiply each part's share, at most the whole", "[hardcore]") {
  const std::string text =
      "RaceWinnings\\DmgPenaltyBody 0.35\nRaceWinnings\\DmgPenaltyBrakes 0.05\n"
      "RaceWinnings\\DmgPenaltyClutch 0.03\nRaceWinnings\\DmgPenaltyDriveline 0.04\n"
      "RaceWinnings\\DmgPenaltyEngine 0.3\nRaceWinnings\\DmgPenaltyGearbox 0.1\n"
      "RaceWinnings\\DmgPenaltySteering 0.07\nRaceWinnings\\DmgPenaltySuspension 0.06\n";
  const auto triple = Scale(text, RepairCostEdits(300).scales);
  REQUIRE(triple);
  CHECK(GetTunable(*triple, "RaceWinnings\\DmgPenaltyBody") == std::optional<std::string>("1"));  // 1.05
  CHECK(GetTunable(*triple, "RaceWinnings\\DmgPenaltyEngine") == std::optional<std::string>("0.9"));
  CHECK(GetTunable(*triple, "RaceWinnings\\DmgPenaltyClutch") == std::optional<std::string>("0.09"));
  CHECK(triple->size() == text.size());
  const auto twice = Scale(text, RepairCostEdits(200).scales);
  REQUIRE(twice);
  CHECK(GetTunable(*twice, "RaceWinnings\\DmgPenaltyBody") == std::optional<std::string>("0.7"));
}

TEST_CASE("A raise never makes a mod's repairs cheaper", "[hardcore]") {
  const std::string text = "RaceWinnings\\DmgPenaltyBody 1.5\n";
  const auto raised = ScaleTunable(text, {"RaceWinnings\\DmgPenaltyBody", 2.0, 1.0, true});
  REQUIRE(raised);
  CHECK(GetTunable(*raised, "RaceWinnings\\DmgPenaltyBody") == std::optional<std::string>("1.5"));
}

TEST_CASE("A factor written as a whole number can still come down", "[hardcore]") {
  const std::string padded = "CarValueScales\\SellValueScale 1   \n";
  const auto lower = ScaleTunable(padded, {"CarValueScales\\SellValueScale", 0.75, std::nullopt, true});
  REQUIRE(lower);
  CHECK(GetTunable(*lower, "CarValueScales\\SellValueScale") == std::optional<std::string>("0.75"));
  CHECK(lower->size() == padded.size());
}

TEST_CASE("A value that cannot be scaled says why", "[hardcore]") {
  std::string why;
  CHECK_FALSE(ScaleTunable("Other\\Key 1\n", {"RaceWinnings\\DmgPenaltyBody", 2.0, 1.0, true}, &why));
  CHECK(why == "is missing");
  CHECK_FALSE(ScaleTunable("RaceWinnings\\DmgPenaltyBody abc\n", {"RaceWinnings\\DmgPenaltyBody", 2.0, 1.0, true}, &why));
  CHECK(why == "is not a number");
}

TEST_CASE("Only the DIFFICULTY options set, and in range, are applied", "[hardcore]") {
  const auto reset = [] {
    for (const char* name : {"fm4_race_payouts", "fm4_repair_costs", "fm4_resale_value"}) {
      rex::cvar::SetFlagByName(name, "100");
    }
  };
  reset();
  CHECK(ActiveHardcoreOptions().empty());
  rex::cvar::SetFlagByName("fm4_race_payouts", "75");
  rex::cvar::SetFlagByName("fm4_resale_value", "50");
  rex::cvar::SetFlagByName("fm4_repair_costs", "250");  // not one of its settings
  const auto options = ActiveHardcoreOptions();
  REQUIRE(options.size() == 2);
  CHECK(options[0].name == "RACE PAYOUTS 75%");
  CHECK(options[1].name == "RESALE VALUE 50%");
  reset();
}
