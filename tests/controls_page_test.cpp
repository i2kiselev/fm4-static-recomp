// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "config/host_config.h"
#include "input/controls.h"
#include "ui/controls_page.h"
#include "ui/hostui/menu.h"

namespace {

namespace config = fm4_static_recomp::config;
namespace ui = fm4_static_recomp::ui;
using fm4_static_recomp::hostui::MenuRow;
using fm4_static_recomp::hostui::MenuScreen;
using fm4_static_recomp::hostui::NavCommand;
using fm4_static_recomp::hostui::PadNavigator;

enum class Tab { kKeyboard, kMouse };

struct Page {
  explicit Page(std::string_view text, uint32_t layout = 0, Tab tab = Tab::kKeyboard)
      : path(std::filesystem::temp_directory_path() / "fm4_controls_page_test.toml"),
        config(path) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    REQUIRE(config.Load());
    ui::ControlsPageServices services;
    services.controller_layout = [layout] { return layout; };
    services.apply = [this] {
      ++applied;
      return apply_works;
    };
    services.now = [this] { return now; };
    screen = tab == Tab::kMouse ? ui::CreateMousePage(config, std::move(services))
                                : ui::CreateControlsPage(config, std::move(services));
  }
  ~Page() { std::filesystem::remove(path); }

  size_t Index(std::string_view label) const {
    const auto& rows = screen->rows();
    for (size_t i = 0; i < rows.size(); ++i) {
      if (rows[i].label == label) {
        return i;
      }
    }
    FAIL("no row " << label);
    return 0;
  }
  const MenuRow& Row(std::string_view label) const { return screen->rows()[Index(label)]; }
  std::vector<std::string> Slots(std::string_view label) const { return Row(label).slots(); }

  void Bind(std::string_view label, size_t slot, const std::string& key) {
    REQUIRE(screen->SetFocus(Index(label)));
    screen->SetSlot(slot);
    screen->Handle(NavCommand::kAccept);
    REQUIRE(screen->capturing());
    screen->CaptureKey(key);
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
  int applied = 0;
  bool apply_works = true;
  std::chrono::steady_clock::time_point now{};
};

}  // namespace

TEST_CASE("The driving rows name what each button does in the profile's layout",
          "[controls_page]") {
  CHECK(Page("").screen->rows()[1].label == "THROTTLE (RT)");
  const Page layout7("", 7);
  CHECK(layout7.screen->rows()[1].label == "GEAR UP (RT)");
  CHECK(layout7.Row("THROTTLE (A)").slots()[0] == "SPACE");
}

TEST_CASE("One list: a DRIVING section with the ramps, then a MENUS section", "[controls_page]") {
  const Page page("");
  const auto& rows = page.screen->rows();
  CHECK(rows[0].label == "DRIVING");
  CHECK(rows[0].heading);
  CHECK(page.screen->focus() == 1);
  const size_t menus = page.Index("MENUS");
  CHECK(rows[menus - 1].label == "LOOKING AROUND");
  CHECK(rows[menus + 1].label == "SELECT (A)");
  CHECK(rows.back().label == "DEFAULTS");
  CHECK(!page.screen->SetFocus(menus));
  REQUIRE(page.screen->SetFocus(menus + 1));
  page.screen->Handle(NavCommand::kUp);
  CHECK(page.screen->focus() == menus - 1);
}

TEST_CASE("A row shows its primary and secondary key", "[controls_page]") {
  const Page page("");
  CHECK(page.Slots("SELECT (A)") == std::vector<std::string>{"ENTER", "SPACE"});
  CHECK(page.Slots("MOVE UP (D-PAD UP)") == std::vector<std::string>{"UP", "W"});
  CHECK(page.Slots("X") == std::vector<std::string>{"X", "NOT SET"});
}

TEST_CASE("A key waits for SAVE & APPLY, which writes only that line and applies it",
          "[controls_page]") {
  Page page("menu_a = \"Return\"\n");
  page.Bind("THROTTLE (RT)", 1, "Up");
  CHECK(page.Slots("THROTTLE (RT)") == std::vector<std::string>{"W", "UP"});
  CHECK(page.Row("THROTTLE (RT)").unapplied());
  CHECK(page.screen->status().text == "UNSAVED CHANGES");
  CHECK(page.File() == "menu_a = \"Return\"\n");
  page.screen->Handle(NavCommand::kApply);
  CHECK(page.applied == 1);
  CHECK(page.File() == "menu_a = \"Return\"\ndriving_right_trigger = \"W,Up\"\n");
  CHECK(!page.Row("THROTTLE (RT)").unapplied());
  CHECK(page.screen->status().text == "KEYS SAVED AND APPLIED.");
}

TEST_CASE("Keys set back to the default remove their line", "[controls_page]") {
  Page page("driving_right_trigger = \"Up\"\n");
  CHECK(page.Slots("THROTTLE (RT)")[0] == "UP");
  page.Bind("THROTTLE (RT)", 0, "W");
  page.screen->Handle(NavCommand::kApply);
  CHECK(page.File() == "\n");
  CHECK(page.Slots("THROTTLE (RT)")[0] == "W");
}

TEST_CASE("A default button whose key another takes shows the key the game gives it",
          "[controls_page]") {
  Page page("");
  page.Bind("THROTTLE (RT)", 0, "Space");
  // LoadControls: the handbrake loses Space and takes the throttle's old key.
  CHECK(page.Slots("HANDBRAKE (A)")[0] == "W");
  CHECK(page.Row("HANDBRAKE (A)").note() ==
        "SPACE is on THROTTLE (RT) now, so the game gives this button W.");
  CHECK(page.Row("THROTTLE (RT)").note().empty());
}

TEST_CASE("Two buttons you set on one key say so", "[controls_page]") {
  Page page("");
  page.Bind("THROTTLE (RT)", 0, "Space");
  page.Bind("BRAKE (LT)", 0, "Space");
  CHECK(page.Row("BRAKE (LT)").note() ==
        "Also used by THROTTLE (RT): one press of the key presses both.");
}

TEST_CASE("A key already in the other slot swaps the two", "[controls_page]") {
  Page page("");
  page.Bind("SELECT (A)", 0, "Space");
  CHECK(page.Slots("SELECT (A)") == std::vector<std::string>{"SPACE", "ENTER"});
}

TEST_CASE("CLEAR removes the secondary key; on the primary it gives back the defaults",
          "[controls_page]") {
  Page page("");
  const size_t select = page.Index("SELECT (A)");
  REQUIRE(page.screen->SetFocus(select));
  page.screen->SetSlot(0);
  CHECK(page.Row("SELECT (A)").clear_label(0).empty());
  page.screen->SetSlot(1);
  CHECK(page.Row("SELECT (A)").clear_label(1) == "CLEAR KEY");
  page.screen->Handle(NavCommand::kClear);
  CHECK(page.Slots("SELECT (A)") == std::vector<std::string>{"ENTER", "NOT SET"});
  CHECK(page.Row("SELECT (A)").clear_label(0) == "DEFAULT KEYS");
  CHECK(page.Row("SELECT (A)").clear_label(1).empty());
  page.screen->SetSlot(0);
  page.screen->Handle(NavCommand::kClear);
  CHECK(page.Slots("SELECT (A)") == std::vector<std::string>{"ENTER", "SPACE"});
  CHECK(!page.Row("SELECT (A)").unapplied());
}

TEST_CASE("A capture ends on back or after five seconds without a key", "[controls_page]") {
  Page page("");
  REQUIRE(page.screen->SetFocus(page.Index("BRAKE (LT)")));
  page.screen->Handle(NavCommand::kAccept);
  CHECK(page.screen->capturing());
  CHECK(page.Slots("BRAKE (LT)")[0] == "PRESS A KEY");
  CHECK(page.screen->status().text == "PRESS THE KEY FOR BRAKE (LT) (CANCELS IN 5 S)");
  CHECK(page.screen->Handle(NavCommand::kDown) == MenuScreen::Result::kNone);
  CHECK(page.screen->Handle(NavCommand::kBack) == MenuScreen::Result::kChanged);
  CHECK(!page.screen->capturing());

  page.screen->Handle(NavCommand::kAccept);
  page.now += ui::kCaptureTimeout;
  page.screen->Update(false);
  CHECK(!page.screen->capturing());
  CHECK(page.Slots("BRAKE (LT)")[0] == "S");
}

TEST_CASE("The ramp rows step by a hundredth and save without quotes", "[controls_page]") {
  Page page("");
  REQUIRE(page.screen->SetFocus(page.Index("STEERING: REST TO FULL")));
  CHECK(page.Row("STEERING: REST TO FULL").value() == "0.17 S");
  page.screen->Handle(NavCommand::kRight);
  CHECK(page.Row("STEERING: REST TO FULL").value() == "0.18 S");
  page.screen->Handle(NavCommand::kApply);
  CHECK(page.File() == "ramp_steer_seconds = 0.18\n");
}

TEST_CASE("The MOUSE tab has mouse look and its sensitivity, and the keys are not on it",
          "[controls_page]") {
  const Page page("", 0, Tab::kMouse);
  CHECK(page.screen->title() == "MOUSE");
  const auto& rows = page.screen->rows();
  REQUIRE(rows.size() == 2);
  CHECK(rows[0].label == "MOUSE LOOK (HOLD RIGHT BUTTON)");
  CHECK(rows[1].label == "MOUSE LOOK SENSITIVITY");
  const Page keyboard("");
  for (const MenuRow& row : keyboard.screen->rows()) {
    CHECK(!row.label.starts_with("MOUSE"));
  }
}

TEST_CASE("Mouse look flips off and saves a bare false", "[controls_page]") {
  Page page("", 0, Tab::kMouse);
  REQUIRE(page.screen->SetFocus(page.Index("MOUSE LOOK (HOLD RIGHT BUTTON)")));
  CHECK(page.Row("MOUSE LOOK (HOLD RIGHT BUTTON)").value() == "ON");
  page.screen->Handle(NavCommand::kRight);
  CHECK(page.Row("MOUSE LOOK (HOLD RIGHT BUTTON)").value() == "OFF");
  page.screen->Handle(NavCommand::kApply);
  CHECK(page.File() == "mouse_look = false\n");
  CHECK(page.applied == 1);
}

TEST_CASE("Mouse look sensitivity steps by one from 10 to 100", "[controls_page]") {
  {
    Page page("", 0, Tab::kMouse);
    REQUIRE(page.screen->SetFocus(page.Index("MOUSE LOOK SENSITIVITY")));
    CHECK(page.Row("MOUSE LOOK SENSITIVITY").value() == "25");
    page.screen->Handle(NavCommand::kRight);
    CHECK(page.Row("MOUSE LOOK SENSITIVITY").value() == "26");
    page.screen->Handle(NavCommand::kApply);
    CHECK(page.File() == "mouse_look_sensitivity = 26\n");
  }
  {
    Page page("mouse_look_sensitivity = 100\n", 0, Tab::kMouse);
    REQUIRE(page.screen->SetFocus(page.Index("MOUSE LOOK SENSITIVITY")));
    page.screen->Handle(NavCommand::kRight);
    CHECK(page.Row("MOUSE LOOK SENSITIVITY").value() == "100");
  }
  {
    Page page("mouse_look_sensitivity = 10\n", 0, Tab::kMouse);
    REQUIRE(page.screen->SetFocus(page.Index("MOUSE LOOK SENSITIVITY")));
    page.screen->Handle(NavCommand::kLeft);
    CHECK(page.Row("MOUSE LOOK SENSITIVITY").value() == "10");
  }
}

TEST_CASE("RESET ALL drafts the defaults of both sections, which remove the lines",
          "[controls_page]") {
  Page page("driving_a = \"B\"\nramp_look_seconds = 0.3\nmenu_a = \"B\"\nmouse_look = false\n");
  REQUIRE(page.screen->SetFocus(page.Index("DEFAULTS")));
  page.screen->Handle(NavCommand::kAccept);
  CHECK(page.Slots("HANDBRAKE (A)")[0] == "SPACE");
  CHECK(page.Slots("SELECT (A)")[0] == "ENTER");
  CHECK(page.Row("LOOKING AROUND").value() == "0.15 S");
  page.screen->Handle(NavCommand::kApply);
  // The mouse's line belongs to the MOUSE tab.
  CHECK(page.File() == "mouse_look = false\n");
}

TEST_CASE("A key you set in one section takes nothing from the other", "[controls_page]") {
  Page page("");
  page.Bind("THROTTLE (RT)", 0, "Return");
  CHECK(page.Slots("SELECT (A)") == std::vector<std::string>{"ENTER", "SPACE"});
  CHECK(page.Row("SELECT (A)").note().empty());
}

TEST_CASE("Leaving the keys with unsaved changes warns once", "[controls_page]") {
  Page page("");
  page.Bind("THROTTLE (RT)", 0, "Up");
  CHECK(!page.screen->MayLeave());
  CHECK(page.screen->status().warning);
  CHECK(page.screen->MayLeave());
}

TEST_CASE("A failed save keeps the keys drafted and the file's values as they were",
          "[controls_page]") {
  Page page("driving_right_trigger = \"Up\"\n");
  page.Bind("THROTTLE (RT)", 0, "W");
  {
    // Block the atomic-write temporary path on Windows and Unix alike.
    auto blocked = page.path;
    blocked += ".tmp";
    std::filesystem::create_directory(blocked);
    std::ofstream(blocked / "occupied") << "blocked";
    page.screen->Handle(NavCommand::kApply);
    std::filesystem::remove_all(blocked);
  }
  CHECK(page.applied == 0);
  CHECK(page.screen->status().text == "COULD NOT SAVE CONTROLS.TOML");
  CHECK(page.Row("THROTTLE (RT)").unapplied());
  CHECK(page.config.Get("driving_right_trigger") == "Up");
  CHECK(!page.config.dirty());
  page.Bind("THROTTLE (RT)", 0, "Up");
  CHECK(!page.Row("THROTTLE (RT)").unapplied());
}

TEST_CASE("Keys saved but not taken in say they apply at the next start", "[controls_page]") {
  Page page("");
  page.apply_works = false;
  page.Bind("THROTTLE (RT)", 0, "Up");
  page.screen->Handle(NavCommand::kApply);
  CHECK(page.screen->status().text == "KEYS SAVED; THEY APPLY AT THE NEXT START.");
}

TEST_CASE("Values written another way compare as the page writes them", "[controls_page]") {
  Page page("driving_right_trigger = \"W, Up\"\nramp_look_seconds = 0.150\n");
  page.Bind("THROTTLE (RT)", 0, "Up");
  CHECK(page.Row("THROTTLE (RT)").unapplied());
  page.Bind("THROTTLE (RT)", 0, "W");
  CHECK(!page.Row("THROTTLE (RT)").unapplied());
  REQUIRE(page.screen->SetFocus(page.Index("LOOKING AROUND")));
  page.screen->Handle(NavCommand::kRight);
  page.screen->Handle(NavCommand::kLeft);
  CHECK(!page.Row("LOOKING AROUND").unapplied());
  CHECK(page.screen->status().text.empty());
}

TEST_CASE("A line of keys the game does not know leaves the button its defaults",
          "[controls_page]") {
  const Page page("driving_a = \"Mouse4\"\n");
  CHECK(page.Slots("HANDBRAKE (A)")[0] == "SPACE");
  CHECK(page.Row("HANDBRAKE (A)").clear_label(0).empty());
}

TEST_CASE("Picking the empty secondary slot waits for the key there", "[controls_page]") {
  Page page("");
  REQUIRE(page.screen->SetFocus(page.Index("X")));
  page.screen->SetSlot(1);
  page.screen->Handle(NavCommand::kAccept);
  CHECK(page.Slots("X") == std::vector<std::string>{"X", "PRESS A KEY"});
  page.screen->CaptureKey("Delete");
  CHECK(page.Slots("X") == std::vector<std::string>{"X", "DEL"});
}

TEST_CASE("The pad's X, LB and RB clear and switch tabs", "[controls_page]") {
  PadNavigator pad;
  pad.Poll(0, 0, 0, 0);
  CHECK(pad.Poll(PadNavigator::kButtonX, 0, 0, 10) == std::vector{NavCommand::kClear});
  CHECK(pad.Poll(PadNavigator::kLeftShoulder, 0, 0, 20) ==
        std::vector{NavCommand::kPreviousTab});
  CHECK(pad.Poll(PadNavigator::kRightShoulder, 0, 0, 30) == std::vector{NavCommand::kNextTab});
}

TEST_CASE("The page's keys follow the menu binds and keep the arrows, Enter and Esc",
          "[menu]") {
  using fm4_static_recomp::hostui::KeyCommand;
  using fm4_static_recomp::hostui::MenuKeys;
  using rex::ui::VirtualKey;
  MenuKeys keys{.up = "Up,W", .down = "Down,S", .left = "Left,A", .right = "Right,D",
                .accept = "Return,Space", .back = "Escape,Backspace", .apply = "Y", .clear = "X",
                .previous_tab = "Q", .next_tab = "E"};
  CHECK(KeyCommand(keys, VirtualKey::kW, false) == NavCommand::kUp);
  CHECK(KeyCommand(keys, VirtualKey::kA, false) == NavCommand::kLeft);
  CHECK(KeyCommand(keys, VirtualKey::kS, true) == NavCommand::kDown);
  CHECK(KeyCommand(keys, VirtualKey::kD, false) == NavCommand::kRight);
  CHECK(KeyCommand(keys, VirtualKey::kNumpad8, false) == NavCommand::kUp);
  CHECK(KeyCommand(keys, VirtualKey::kY, false) == NavCommand::kApply);
  CHECK(KeyCommand(keys, VirtualKey::kX, false) == NavCommand::kClear);
  CHECK(KeyCommand(keys, VirtualKey::kE, false) == NavCommand::kNextTab);
  CHECK(KeyCommand(keys, VirtualKey::kZ, false) == std::nullopt);
  // Buttons fire once per press; directions repeat.
  CHECK(KeyCommand(keys, VirtualKey::kSpace, true) == std::nullopt);

  // Rebound menu keys: the new ones work, the fixed ones still do.
  keys.up = "I";
  keys.accept = "F";
  keys.back = "G";
  CHECK(KeyCommand(keys, VirtualKey::kI, false) == NavCommand::kUp);
  CHECK(KeyCommand(keys, VirtualKey::kW, false) == std::nullopt);
  CHECK(KeyCommand(keys, VirtualKey::kUp, false) == NavCommand::kUp);
  CHECK(KeyCommand(keys, VirtualKey::kF, false) == NavCommand::kAccept);
  CHECK(KeyCommand(keys, VirtualKey::kReturn, false) == NavCommand::kAccept);
  CHECK(KeyCommand(keys, VirtualKey::kG, false) == NavCommand::kBack);
  CHECK(KeyCommand(keys, VirtualKey::kEscape, false) == NavCommand::kBack);
}

TEST_CASE("Pressed keys get their bind names; function keys and mouse buttons none",
          "[controls_page]") {
  using fm4_static_recomp::input::BindKeyName;
  using rex::ui::VirtualKey;
  CHECK(BindKeyName(VirtualKey::kReturn) == "Return");
  CHECK(BindKeyName(VirtualKey::kA) == "A");
  CHECK(BindKeyName(VirtualKey::kShift) == "Shift");
  CHECK(BindKeyName(VirtualKey::kOemComma) == "Comma");
  CHECK(BindKeyName(VirtualKey::kF6).empty());
  CHECK(BindKeyName(VirtualKey::kLButton).empty());
}
