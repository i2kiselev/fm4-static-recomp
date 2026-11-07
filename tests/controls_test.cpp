#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>

#include <rex/input/mnk/mnk_bind.h>

#include "config/host_config.h"
#include "input/controls.h"

using fm4_static_recomp::config::HostConfig;
using namespace fm4_static_recomp::input;

namespace {

const std::string& Keys(const BindingSet& set, PadControl control) {
  return set.keys[size_t(control)];
}

bool Contains(std::string_view bind, std::string_view key) {
  while (!bind.empty()) {
    const size_t comma = bind.find(',');
    if (bind.substr(0, comma) == key)
      return true;
    bind = comma == std::string_view::npos ? std::string_view() : bind.substr(comma + 1);
  }
  return false;
}

Controls LoadFrom(std::string_view text) {
  const auto path = std::filesystem::temp_directory_path() / "fm4_controls_test.toml";
  std::ofstream(path, std::ios::binary) << text;
  HostConfig config(path);
  REQUIRE(config.Load());
  Controls controls = LoadControls(config);
  std::filesystem::remove(path);
  return controls;
}

}  // namespace

TEST_CASE("The default keys follow the Forza Horizon 6 layout on FM4's pad layout", "[controls]") {
  const Controls controls = DefaultControls();
  const BindingSet& driving = controls.driving;
  CHECK(Contains(Keys(driving, PadControl::kRightTrigger), "W"));
  CHECK(Contains(Keys(driving, PadControl::kLeftTrigger), "S"));
  CHECK(Contains(Keys(driving, PadControl::kLStickLeft), "A"));
  CHECK(Contains(Keys(driving, PadControl::kLStickRight), "D"));
  CHECK(Contains(Keys(driving, PadControl::kRStickDown), "Down"));
  // FM4's controller layout 0: A handbrake, B gear up, X gear down, Y rewind,
  // LB clutch, D-pad up telemetry; BACK changes the camera, START pauses.
  CHECK(Contains(Keys(driving, PadControl::kA), "Space"));
  CHECK(Contains(Keys(driving, PadControl::kB), "E"));
  CHECK(Contains(Keys(driving, PadControl::kX), "Q"));
  CHECK(Contains(Keys(driving, PadControl::kY), "R"));
  CHECK(Contains(Keys(driving, PadControl::kLeftShoulder), "Shift"));
  CHECK(Contains(Keys(driving, PadControl::kDpadUp), "T"));
  CHECK(Contains(Keys(driving, PadControl::kBack), "Tab"));
  CHECK(Contains(Keys(driving, PadControl::kStart), "Escape"));

  const BindingSet& menu = controls.menu;
  CHECK(Keys(menu, PadControl::kA).starts_with("Return"));
  CHECK(Keys(menu, PadControl::kB) == "Escape,Backspace");
  // WASD press the D-pad like the arrows: FM4's sliders step on a D-pad tap but
  // not on a short push of the stick.
  CHECK(Keys(menu, PadControl::kDpadUp) == "Up,W");
  CHECK(Keys(menu, PadControl::kDpadLeft) == "Left,A");
  CHECK(Keys(menu, PadControl::kLStickUp) == "Numpad8");
}

TEST_CASE("Every pad control has a key in both sets", "[controls]") {
  const Controls controls = DefaultControls();
  for (size_t i = 0; i < size_t(PadControl::kCount); ++i) {
    INFO("control " << i);
    CHECK_FALSE(controls.menu.keys[i].empty());
    CHECK_FALSE(controls.driving.keys[i].empty());
  }
}

TEST_CASE("No default key is a host key", "[controls]") {
  // The binds the SDK (rex_app.cpp) and FM4 (fm4_app.h) register by default.
  const std::set<std::string> host_keys = {"F3", "F4", "F6", "F7", "Backtick"};
  const Controls controls = DefaultControls();
  for (const BindingSet* set : {&controls.menu, &controls.driving}) {
    for (const std::string& bind : set->keys) {
      for (const std::string& host : host_keys) {
        INFO(bind);
        CHECK_FALSE(Contains(bind, host));
      }
    }
  }
}

TEST_CASE("A file entry overrides one binding", "[controls]") {
  const Controls controls = LoadFrom("menu_a = \"Space\"\n");
  const Controls defaults = DefaultControls();
  CHECK(Keys(controls.menu, PadControl::kA) == "Space");
  for (size_t i = 1; i < size_t(PadControl::kCount); ++i) {
    CHECK(controls.menu.keys[i] == defaults.menu.keys[i]);
  }
  CHECK(controls.driving.keys == defaults.driving.keys);
}

TEST_CASE("A default control keeps its other keys when the player takes one", "[controls]") {
  const Controls controls = LoadFrom("menu_x = \"Space\"\n");
  CHECK(Keys(controls.menu, PadControl::kX) == "Space");
  CHECK(Keys(controls.menu, PadControl::kA) == "Return");
}

TEST_CASE("A default control whose only key the player takes gets the taker's old key",
          "[controls]") {
  const Controls controls = LoadFrom("driving_b = \"C\"\n");
  CHECK(Keys(controls.driving, PadControl::kB) == "C");
  CHECK(Keys(controls.driving, PadControl::kRightShoulder) == "E");
}

TEST_CASE("Without a free old key of the taker a spare key that nothing uses is taken",
          "[controls]") {
  const Controls controls =
      LoadFrom("driving_b = \"C\"\ndriving_x = \"E\"\ndriving_y = \"F\"\n");
  CHECK(Keys(controls.driving, PadControl::kRightShoulder) == "Z");
}

TEST_CASE("A key under a modifier counts as that key", "[controls]") {
  CHECK(Keys(LoadFrom("driving_b = \"Control+C\"\n").driving, PadControl::kRightShoulder) == "E");
  const Controls controls =
      LoadFrom("driving_b = \"C\"\ndriving_x = \"E\"\ndriving_y = \"Shift+F\"\n");
  CHECK(Keys(controls.driving, PadControl::kRightShoulder) == "Z");
}

TEST_CASE("A driving line leaves the menu keys alone", "[controls]") {
  CHECK(LoadFrom("driving_b = \"C\"\n").menu.keys == DefaultControls().menu.keys);
}

TEST_CASE("The menu set swaps like the driving set", "[controls]") {
  const Controls controls = LoadFrom("menu_a = \"E\"\n");
  CHECK(Keys(controls.menu, PadControl::kA) == "E");
  CHECK(Keys(controls.menu, PadControl::kRightShoulder) == "Return");
}

TEST_CASE("With every spare key taken a control shares its default key", "[controls]") {
  const Controls controls = LoadFrom(
      "driving_a = \"Space,C,F,Z,X,B,N,M,Y,U,O,H,V,R,T,G,J,K,L,I,P,1,2,3,4,5,6\"\n");
  CHECK(Keys(controls.driving, PadControl::kRightShoulder) == "C");
  CHECK(Keys(controls.driving, PadControl::kY) == "R");
}

TEST_CASE("The player's own lines keep a key they share", "[controls]") {
  const Controls controls = LoadFrom("driving_a = \"C\"\ndriving_b = \"C\"\n");
  CHECK(Keys(controls.driving, PadControl::kA) == "C");
  CHECK(Keys(controls.driving, PadControl::kB) == "C");
  CHECK(Keys(controls.driving, PadControl::kRightShoulder) == "Space");
}

TEST_CASE("Rebinding leaves every control a key and no key on two controls", "[controls]") {
  // A player's file from issue #59.
  const Controls controls = LoadFrom(
      "driving_rstick_left = \"Q\"\ndriving_rstick_right = \"E\"\n"
      "driving_rstick_down = \"Shift\"\ndriving_x = \"X\"\ndriving_b = \"C\"\n");
  const BindingSet& driving = controls.driving;
  CHECK(Keys(driving, PadControl::kLeftShoulder) == "Down");
  CHECK(Keys(driving, PadControl::kRightShoulder) == "F");
  std::set<std::string> seen;
  for (size_t i = 0; i < size_t(PadControl::kCount); ++i) {
    INFO("control " << i << ": " << driving.keys[i]);
    CHECK_FALSE(driving.keys[i].empty());
    std::string_view bind = driving.keys[i];
    while (!bind.empty()) {
      const size_t comma = bind.find(',');
      CHECK(seen.insert(std::string(bind.substr(0, comma))).second);
      bind = comma == std::string_view::npos ? std::string_view() : bind.substr(comma + 1);
    }
  }
}

TEST_CASE("Unknown names and empty values keep the defaults", "[controls]") {
  const Controls controls = LoadFrom("menu_zz = \"Q\"\nmenu_b = \"\"\n");
  const Controls defaults = DefaultControls();
  CHECK(controls.menu.keys == defaults.menu.keys);
  CHECK(controls.driving.keys == defaults.driving.keys);
}

TEST_CASE("Ramp rates are read from the file", "[controls]") {
  CHECK(LoadFrom("ramp_steer_seconds = 0.3\n").ramp.steer_seconds == 0.3);
  CHECK(LoadFrom("ramp_steer_seconds = abc\n").ramp.steer_seconds ==
        DefaultControls().ramp.steer_seconds);
  CHECK(LoadFrom("ramp_look_seconds = 0.25\n").ramp.look_seconds == 0.25);
}

TEST_CASE("Ramp rates accept decimal notation and reject malformed or out-of-range values", "[controls]") {
  CHECK(LoadFrom("ramp_steer_seconds = 3e-1\n").ramp.steer_seconds == 0.3);
  CHECK(LoadFrom("ramp_steer_seconds = 0\n").ramp.steer_seconds == 0.0);
  for (std::string_view token : {"+0.3", " 0.3", "0.3 ", "0x1p0", "-0.5", "0.3tail",
                                 "1e9999", "1e-9999", "0,3", "nan"}) {
    CAPTURE(token);
    CHECK(LoadFrom("ramp_steer_seconds = \"" + std::string(token) + "\"\n").ramp.steer_seconds ==
          DefaultControls().ramp.steer_seconds);
  }
}

TEST_CASE("A missing file loads the defaults without writing one", "[controls]") {
  const auto path = std::filesystem::temp_directory_path() / "fm4_controls_missing.toml";
  std::filesystem::remove(path);
  HostConfig config(path);
  REQUIRE(config.Load());
  const Controls controls = LoadControls(config);
  CHECK(controls.menu.keys == DefaultControls().menu.keys);
  CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("Applying a set hands every control's keys to the keyboard driver", "[controls]") {
  const Controls controls = DefaultControls();
  ApplyBindings(controls.driving);
  const auto overrides = rex::input::mnk::KeybindOverrides();
  REQUIRE(overrides != nullptr);
  CHECK(overrides->at("keybind_a") == "Space");
  CHECK(overrides->at("keybind_right_trigger") == "W");
  CHECK(overrides->at("keybind_start") == "Escape");
  CHECK(overrides->size() == size_t(PadControl::kCount));
  rex::input::mnk::SetKeybindOverrides(nullptr);
}

TEST_CASE("Keys the keyboard driver does not know are left out", "[controls]") {
  const Controls defaults = DefaultControls();
  // The keycap says ENTER, the key is called Return.
  CHECK(LoadFrom("menu_a = \"Enter\"\n").menu.keys[size_t(PadControl::kA)] ==
        defaults.menu.keys[size_t(PadControl::kA)]);
  CHECK(LoadFrom("menu_a = \"Enter, Space\"\n").menu.keys[size_t(PadControl::kA)] == "Space");
  CHECK(LoadFrom("driving_x = \"Shift+W,shift\"\n").driving.keys[size_t(PadControl::kX)] ==
        "Shift+W");
}
