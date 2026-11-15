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
    rex::cvar::SetFlagByName("fm4_quick_race_laps", "0");
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    REQUIRE(config.Load());
    screen = ui::CreateGameplayPage(config);
    REQUIRE(screen->SetFocus(0));
  }
  ~Page() {
    std::filesystem::remove(path);
    rex::cvar::SetFlagByName("fm4_quick_race_laps", "0");
  }

  std::string Value() const { return screen->rows()[0].value(); }
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
