// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include <rex/cvar.h>

#include "config/host_config.h"
#include "hardcore.h"
#include "quick_race_laps.h"
#include "ui/gameplay_page.h"
#include "ui/hostui/menu.h"

namespace {

namespace config = fm4_static_recomp::config;
namespace ui = fm4_static_recomp::ui;
using fm4_static_recomp::QuickRaceLaps;
using fm4_static_recomp::QuickRaceLapsQuery;
using fm4_static_recomp::QuickRaceLapsSetting;
using fm4_static_recomp::hostui::MenuScreen;
using fm4_static_recomp::hostui::NavCommand;

const std::string kTrackList =
    "SELECT Tracks.id AS Id, Tracks.DisplayName AS DisplayName, Length, RibbonIndex, "
    "MiniRibbonPath, EnvironmentTimeOfDay.Description AS TODName, QuickRaceLapCount FROM "
    "Tracks INNER JOIN Environments ON Tracks.EnvironmentId=Environments.ID WHERE "
    "Tracks.EnvironmentId=%d AND Active=1 AND RibbonConfiguration!=%d ORDER BY MPSortOrder, Id";

struct Page {
  explicit Page(std::string_view text)
      : path(std::filesystem::temp_directory_path() / "fm4_gameplay_page_test.toml"),
        config(path) {
    ResetFlags();
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    REQUIRE(config.Load());
    screen = ui::CreateGameplayPage(config);
    REQUIRE(screen->SetFocus(0));
  }
  ~Page() {
    std::filesystem::remove(path);
    ResetFlags();
  }

  static void ResetFlags() {
    rex::cvar::SetFlagByName("fm4_quick_race_laps", "0");
    for (const char* name : {"fm4_race_payouts", "fm4_repair_costs", "fm4_resale_value"}) {
      rex::cvar::SetFlagByName(name, "100");
    }
  }

  std::string Value(size_t row = 0) const { return screen->rows()[row].value(); }
  // Moves the focus to `row` and steps its value.
  void Step(size_t row, NavCommand command) {
    REQUIRE(screen->SetFocus(row));
    screen->Handle(command);
  }
  std::string File() const {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
  }

  std::filesystem::path path;
  config::HostConfig config;
  std::unique_ptr<MenuScreen> screen;
};

}  // namespace

TEST_CASE("Race setup takes the setting on tracks with a Quick Race count", "[quick_race_laps]") {
  CHECK(QuickRaceLaps(3, 2, 0) == 3);
  CHECK(QuickRaceLaps(3, 2, 12) == 12);
  CHECK(QuickRaceLaps(5, 5, 1) == 1);
  CHECK(QuickRaceLaps(0, 2, 12) == 0);
  CHECK(QuickRaceLaps(4, 1, 12) == 4);
  CHECK(QuickRaceLaps(4, 0x22, 12) == 4);
}

TEST_CASE("Track select's query gives the setting under the same column name",
          "[quick_race_laps]") {
  CHECK_FALSE(QuickRaceLapsQuery(kTrackList, 0));
  CHECK_FALSE(QuickRaceLapsQuery(
      "SELECT QuickRaceLapCount, QuickRaceLapCountKinect FROM Tracks WHERE Id=%d", 12));
  const auto query = QuickRaceLapsQuery(kTrackList, 12);
  REQUIRE(query);
  CHECK(*query ==
        "SELECT Tracks.id AS Id, Tracks.DisplayName AS DisplayName, Length, RibbonIndex, "
        "MiniRibbonPath, EnvironmentTimeOfDay.Description AS TODName, (CASE WHEN "
        "QuickRaceLapCount > 0 THEN 12 ELSE QuickRaceLapCount END) AS QuickRaceLapCount FROM "
        "Tracks INNER JOIN Environments ON Tracks.EnvironmentId=Environments.ID WHERE "
        "Tracks.EnvironmentId=%d AND Active=1 AND RibbonConfiguration!=%d ORDER BY "
        "MPSortOrder, Id");
}

TEST_CASE("QUICK RACE LAPS steps from DEFAULT to 99 and saves on apply", "[gameplay_page]") {
  Page page("");
  CHECK(page.screen->title() == "GAMEPLAY");
  CHECK(page.screen->rows()[0].label == "QUICK RACE LAPS");
  CHECK(page.Value() == "DEFAULT");
  page.screen->Handle(NavCommand::kLeft);
  CHECK(page.Value() == "DEFAULT");
  page.screen->Handle(NavCommand::kRight);
  page.screen->Handle(NavCommand::kRight);
  CHECK(page.Value() == "2");
  CHECK(page.screen->rows()[0].unapplied());
  CHECK(QuickRaceLapsSetting() == 0);
  page.screen->Handle(NavCommand::kApply);
  CHECK(page.File() == "fm4_quick_race_laps = 2\n");
  CHECK(QuickRaceLapsSetting() == 2);
  CHECK_FALSE(page.screen->rows()[0].unapplied());
}

TEST_CASE("DEFAULT removes the saved count", "[gameplay_page]") {
  Page page("fm4_quick_race_laps = 1\n");
  ui::ApplySavedGameplaySettings(page.config);
  CHECK(QuickRaceLapsSetting() == 1);
  CHECK(page.Value() == "1");
  page.screen->Handle(NavCommand::kLeft);
  CHECK(page.Value() == "DEFAULT");
  page.screen->Handle(NavCommand::kApply);
  CHECK(page.File().find("fm4_quick_race_laps") == std::string::npos);
  CHECK(QuickRaceLapsSetting() == 0);
}

TEST_CASE("A saved count out of range is ignored", "[gameplay_page]") {
  Page page("fm4_quick_race_laps = 150\n");
  ui::ApplySavedGameplaySettings(page.config);
  CHECK(QuickRaceLapsSetting() == 0);
}

TEST_CASE("DIFFICULTY has its three settings, each the game's own by default", "[gameplay_page]") {
  Page page("");
  fm4_static_recomp::InstallHardcoreOptions({});  // the game started with them all off
  const auto& rows = page.screen->rows();
  CHECK(rows[1].label == "DIFFICULTY");
  CHECK_FALSE(rows[1].is_enabled());  // a heading
  CHECK(rows[2].label == "RACE PAYOUTS");
  CHECK(rows[3].label == "REPAIR COSTS");
  CHECK(rows[4].label == "RESALE VALUE");
  CHECK(rows.size() == 5);
  for (size_t row = 2; row <= 4; ++row) {
    CHECK(page.Value(row) == "DEFAULT");
    CHECK(rows[row].note() == "");
  }
}

TEST_CASE("DIFFICULTY settings step through their choices and save for the next start",
          "[gameplay_page]") {
  Page page("");
  fm4_static_recomp::InstallHardcoreOptions({});
  const auto& rows = page.screen->rows();
  page.Step(2, NavCommand::kRight);
  CHECK(page.Value(2) == "-25%");
  page.Step(2, NavCommand::kRight);
  CHECK(page.Value(2) == "-50%");
  page.Step(3, NavCommand::kRight);
  CHECK(page.Value(3) == "+100%");
  page.Step(4, NavCommand::kLeft);  // rolls over to the last
  CHECK(page.Value(4) == "-50%");
  CHECK(rows[2].unapplied());
  CHECK(rows[2].note() == "From the next start.");
  page.screen->Handle(NavCommand::kApply);
  CHECK(page.File() ==
        "fm4_race_payouts = 50\nfm4_repair_costs = 200\nfm4_resale_value = 50\n");
  CHECK(rex::cvar::GetFlagByName("fm4_race_payouts") == "50");
  // The game started with the game's own: until the next start, the rows say so.
  CHECK(rows[2].note() == "From the next start.");
  page.Step(2, NavCommand::kRight);  // back round to the game's own
  CHECK(page.Value(2) == "DEFAULT");
  CHECK(rows[2].note() == "");
}

TEST_CASE("Saved DIFFICULTY settings are applied at startup, ones out of range ignored",
          "[gameplay_page]") {
  {
    Page page("fm4_race_payouts = 75\nfm4_resale_value = 50\n");
    ui::ApplySavedGameplaySettings(page.config);
    CHECK(rex::cvar::GetFlagByName("fm4_race_payouts") == "75");
    CHECK(rex::cvar::GetFlagByName("fm4_resale_value") == "50");
    CHECK(page.Value(2) == "-25%");
    CHECK(page.Value(4) == "-50%");
  }
  Page page("fm4_race_payouts = 10\nfm4_repair_costs = 1000\n");
  ui::ApplySavedGameplaySettings(page.config);
  CHECK(rex::cvar::GetFlagByName("fm4_race_payouts") == "100");
  CHECK(rex::cvar::GetFlagByName("fm4_repair_costs") == "100");
}
