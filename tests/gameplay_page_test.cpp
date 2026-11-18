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
#include "quick_restart.h"
#include "ui/gameplay_page.h"
#include "ui/hostui/menu.h"

namespace {

namespace config = fm4_static_recomp::config;
namespace ui = fm4_static_recomp::ui;
using fm4_static_recomp::QuickRaceLaps;
using fm4_static_recomp::QuickRaceLapsQuery;
using fm4_static_recomp::AutoRestart;
using fm4_static_recomp::ConfiguredAutoRestart;
using fm4_static_recomp::ConfiguredQuickRestartButton;
using fm4_static_recomp::QuickRaceLapsSetting;
using fm4_static_recomp::QuickRestartTap;
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
    rex::cvar::SetFlagByName("fm4_quick_restart", "off");
    rex::cvar::SetFlagByName("fm4_quick_restart_trigger", "hold");
    rex::cvar::SetFlagByName("fm4_auto_restart_dirty_lap", "off");
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

TEST_CASE("QUICK RESTART is off by default, its button and trigger greyed out", "[gameplay_page]") {
  Page page("");
  const auto& rows = page.screen->rows();
  CHECK(rows[1].label == "QUICK RESTART");
  CHECK(rows[2].label == "QUICK RESTART BUTTON");
  CHECK(rows[3].label == "QUICK RESTART TRIGGER");
  CHECK(page.Value(1) == "OFF");
  CHECK(page.Value(2) == "D-PAD LEFT");
  CHECK(page.Value(3) == "HOLD");
  CHECK_FALSE(rows[2].is_enabled());
  CHECK_FALSE(rows[3].is_enabled());
  CHECK(ConfiguredQuickRestartButton() == 0);
}

TEST_CASE("QUICK RESTART switched on saves its button and applies at once", "[gameplay_page]") {
  Page page("");
  page.Step(1, NavCommand::kRight);
  CHECK(page.Value(1) == "ON");
  CHECK(page.screen->rows()[1].unapplied());
  CHECK(ConfiguredQuickRestartButton() == 0);
  page.Step(2, NavCommand::kRight);
  CHECK(page.Value(2) == "D-PAD RIGHT");
  page.Step(2, NavCommand::kRight);
  CHECK(page.Value(2) == "D-PAD LEFT");
  page.Step(3, NavCommand::kRight);
  CHECK(page.Value(3) == "TAP");
  page.screen->Handle(NavCommand::kApply);
  CHECK(page.File() == "fm4_quick_restart = \"dpad_left\"\nfm4_quick_restart_trigger = \"tap\"\n");
  CHECK(ConfiguredQuickRestartButton() == 0x0004);
  CHECK(QuickRestartTap());
}

TEST_CASE("QUICK RESTART switched off keeps the button for next time", "[gameplay_page]") {
  Page page("fm4_quick_restart = \"dpad_right\"\n");
  ui::ApplySavedGameplaySettings(page.config);
  CHECK(ConfiguredQuickRestartButton() == 0x0008);
  CHECK(page.Value(1) == "ON");
  page.Step(1, NavCommand::kRight);
  CHECK(page.Value(1) == "OFF");
  CHECK(page.Value(2) == "D-PAD RIGHT");
  page.screen->Handle(NavCommand::kApply);
  CHECK(ConfiguredQuickRestartButton() == 0);
  page.Step(1, NavCommand::kRight);
  CHECK(page.Value(2) == "D-PAD RIGHT");
}

TEST_CASE("A saved QUICK RESTART button other than the D-pad sides is ignored", "[gameplay_page]") {
  Page page("fm4_quick_restart = \"x\"\n");
  ui::ApplySavedGameplaySettings(page.config);
  CHECK(ConfiguredQuickRestartButton() == 0);
  CHECK(page.Value(1) == "OFF");
}

TEST_CASE("AUTO RESTART ON DIRTY LAP is off by default and greyed out until QUICK RESTART is on",
          "[gameplay_page]") {
  Page page("");
  const auto& rows = page.screen->rows();
  CHECK(rows[4].label == "AUTO RESTART ON DIRTY LAP");
  CHECK(page.Value(4) == "OFF");
  CHECK_FALSE(rows[4].is_enabled());
  CHECK_FALSE(page.screen->SetFocus(4));  // greyed out: it can't be selected
  page.Step(1, NavCommand::kRight);  // QUICK RESTART on
  CHECK(rows[4].is_enabled());
  page.Step(4, NavCommand::kRight);
  CHECK(page.Value(4) == "HOT LAP");
  CHECK(rows[4].unapplied());
  CHECK(ConfiguredAutoRestart() == AutoRestart::kOff);
  page.screen->Handle(NavCommand::kApply);
  CHECK(page.File() ==
        "fm4_auto_restart_dirty_lap = \"hot_lap\"\nfm4_quick_restart = \"dpad_left\"\n");
  CHECK(ConfiguredAutoRestart() == AutoRestart::kHotLap);
  page.Step(1, NavCommand::kRight);  // QUICK RESTART off again: greyed out, its value kept
  CHECK_FALSE(rows[4].is_enabled());
  CHECK(page.Value(4) == "HOT LAP");
}

TEST_CASE("AUTO RESTART ON DIRTY LAP goes through HOT LAP, RACES and both", "[gameplay_page]") {
  Page page("fm4_quick_restart = \"dpad_left\"\n");
  ui::ApplySavedGameplaySettings(page.config);
  page.Step(4, NavCommand::kRight);
  CHECK(page.Value(4) == "HOT LAP");
  page.Step(4, NavCommand::kRight);
  CHECK(page.Value(4) == "RACES");
  page.Step(4, NavCommand::kRight);
  CHECK(page.Value(4) == "HOT LAP + RACES");
  page.screen->Handle(NavCommand::kApply);
  CHECK(ConfiguredAutoRestart() == AutoRestart::kAll);
  page.Step(4, NavCommand::kLeft);
  CHECK(page.Value(4) == "RACES");
}

TEST_CASE("A saved AUTO RESTART is applied at startup, a bad one ignored", "[gameplay_page]") {
  {
    Page page("fm4_auto_restart_dirty_lap = \"races\"\n");
    ui::ApplySavedGameplaySettings(page.config);
    CHECK(ConfiguredAutoRestart() == AutoRestart::kRaces);
    CHECK(page.Value(4) == "RACES");
  }
  Page page("fm4_auto_restart_dirty_lap = \"often\"\n");
  ui::ApplySavedGameplaySettings(page.config);
  CHECK(ConfiguredAutoRestart() == AutoRestart::kOff);
  CHECK(page.Value(4) == "OFF");
}
