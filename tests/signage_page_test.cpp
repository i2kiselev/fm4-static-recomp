// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include <rex/cvar.h>

#include "track_signage.h"
#include "track_signage_runtime.h"
#include "ui/hostui/menu.h"
#include "ui/signage_page.h"

namespace ui = fm4_static_recomp::ui;
using fm4_static_recomp::hostui::MenuScreen;
using fm4_static_recomp::hostui::NavCommand;

namespace {

struct Page {
  Page() {
    rex::cvar::SetFlagByName("fm4_track_signage", "true");
    fm4_static_recomp::signage::SetChoicesFile(file);
    screen = ui::CreateSignagePage();
  }
  ~Page() {
    rex::cvar::SetFlagByName("fm4_track_signage", "true");
    fm4_static_recomp::signage::SetChoicesFile({});
    std::error_code ec;
    std::filesystem::remove(file, ec);
  }
  std::filesystem::path file = std::filesystem::temp_directory_path() / "fm4_signage_page_test.toml";
  const std::vector<fm4_static_recomp::hostui::MenuRow>& Rows() const { return screen->rows(); }
  std::unique_ptr<MenuScreen> screen;
};

}  // namespace

TEST_CASE("SIGNAGE lists the switch, the tracks and the folder", "[signage_page]") {
  Page page;
  CHECK(page.screen->title() == "SIGNAGE");
  const auto& rows = page.Rows();
  REQUIRE(rows.size() == 5);
  CHECK(rows[0].label == "TRACK SIGNAGE");
  CHECK(rows[0].value() == "ON");
  CHECK(rows[1].label == "TRACKS");
  CHECK_FALSE(rows[1].is_enabled());
  CHECK(rows[2].label == "HOCKENHEIMRING");
  CHECK(rows[2].value() == "33 BOARDS");
  CHECK(rows[2].note() == "Every board shows its default.");
  CHECK(rows[3].label == "PICTURES");
  CHECK(rows[4].label == "SIGNAGE FOLDER");
  CHECK(rows[4].value() == "OPEN");
  CHECK(page.screen->focus() == 0);
}

TEST_CASE("A track opens its boards, and B comes back to it", "[signage_page]") {
  Page page;
  REQUIRE(page.screen->SetFocus(2));
  page.screen->Handle(NavCommand::kAccept);
  const auto& rows = page.Rows();
  REQUIRE(rows.size() == 1 + 33 + 2);
  CHECK(rows[0].label == "HOCKENHEIMRING");
  CHECK_FALSE(rows[0].is_enabled());
  CHECK(page.screen->focus() == 1);  // the first board
  CHECK(rows[1].value() == "DEFAULT");
  CHECK(rows[1].note() == "Shows the default board.");
  CHECK(rows.back().label == "RESET ALL BOARDS");
  CHECK_FALSE(rows.back().is_enabled());  // nothing to reset
  // No pictures in the folder: DEFAULT is the only choice.
  page.screen->Handle(NavCommand::kRight);
  CHECK(rows[1].value() == "DEFAULT");
  CHECK(page.screen->Handle(NavCommand::kBack) == MenuScreen::Result::kChanged);  // stays open
  REQUIRE(page.Rows().size() == 5);
  CHECK(page.screen->focus() == 2);  // on the track again
}

TEST_CASE("TRACK SIGNAGE OFF is drafted, then applied with SAVE & APPLY", "[signage_page]") {
  Page page;
  page.screen->Handle(NavCommand::kRight);
  CHECK(page.Rows()[0].value() == "OFF");
  CHECK(page.Rows()[0].unapplied());
  CHECK(rex::cvar::GetFlagByName("fm4_track_signage") == "true");
  CHECK(page.screen->status().text == "UNSAVED CHANGES");
  // Leaving with a change unapplied asks once.
  CHECK(page.screen->Handle(NavCommand::kBack) == MenuScreen::Result::kChanged);
  CHECK(page.screen->status().warning);
  page.screen->Handle(NavCommand::kApply);
  CHECK(rex::cvar::GetFlagByName("fm4_track_signage") == "false");
  CHECK_FALSE(page.Rows()[0].unapplied());
  CHECK(page.screen->status().text == "SAVED. THE BOARDS CHANGE ON TRACK NOW.");
  CHECK(page.screen->Handle(NavCommand::kBack) == MenuScreen::Result::kBack);
}

TEST_CASE("Closing the menu on a track's boards closes it; only B steps back", "[signage_page]") {
  Page page;
  page.screen->Handle(NavCommand::kDown);
  page.screen->Handle(NavCommand::kAccept);
  REQUIRE(page.Rows()[0].label == "HOCKENHEIMRING");
  CHECK(page.screen->Handle(NavCommand::kClose) == MenuScreen::Result::kBack);
}
