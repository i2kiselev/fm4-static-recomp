// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>

#include "config/host_config.h"
#include "ui/hostui/menu.h"
#include "ui/wheel_page.h"
#include "wheel_settings.h"

namespace {

namespace config = fm4_static_recomp::config;
namespace ui = fm4_static_recomp::ui;
using fm4_static_recomp::WheelRow;
using fm4_static_recomp::WheelRows;
using fm4_static_recomp::hostui::MenuScreen;
using fm4_static_recomp::hostui::NavCommand;

struct Page {
  explicit Page(std::string_view text)
      : path(std::filesystem::temp_directory_path() / "fm4_wheel_page_test.toml"), config(path) {
    ResetCvars();
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    REQUIRE(config.Load());
    screen = ui::CreateWheelPage(config);
  }
  ~Page() {
    std::filesystem::remove(path);
    ResetCvars();
  }
  static void ResetCvars() {
    for (const WheelRow& row : WheelRows()) {
      rex::cvar::ResetToDefault(row.key);
    }
    rex::cvar::ResetToDefault("wheel_steering_scale");
  }

  size_t Row(std::string_view label) const {
    for (size_t i = 0; i < screen->rows().size(); ++i) {
      if (screen->rows()[i].label == label) {
        return i;
      }
    }
    FAIL("no row " << label);
    return 0;
  }
  std::string Value(std::string_view label) const { return screen->rows()[Row(label)].value(); }
  void Adjust(std::string_view label, NavCommand command) {
    REQUIRE(screen->SetFocus(Row(label)));
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

TEST_CASE("WHEEL has the wheel type and the nine force rows", "[wheel]") {
  Page page("");
  CHECK(page.screen->title() == "WHEEL");
  std::vector<std::string> labels;
  for (const auto& row : page.screen->rows()) {
    labels.push_back(row.label);
  }
  CHECK(labels == std::vector<std::string>{"WHEEL TYPE", "STRENGTH", "STEERING WEIGHT",
                                           "SELF-CENTRING", "DAMPING", "ROAD EFFECTS",
                                           "ENGINE VIBRATION", "CENTRE FEEL", "SMOOTH JOLTS",
                                           "STEERING LOCK"});
  CHECK(page.Value("WHEEL TYPE") == "DIRECT DRIVE");
  CHECK(page.Value("STRENGTH") == "85 %");
  CHECK(page.Value("STEERING LOCK") == "900 DEGREES");
}

TEST_CASE("Changing DAMPING and applying makes the wheel type CUSTOM", "[wheel]") {
  Page page("wheel_type = \"belt\"\n");
  page.Adjust("DAMPING", NavCommand::kRight);
  CHECK(page.Value("DAMPING") == "90 %");
  CHECK(page.screen->rows()[page.Row("DAMPING")].unapplied());
  page.screen->Handle(NavCommand::kApply);
  CHECK(page.Value("WHEEL TYPE") == "CUSTOM");
  CHECK(page.config.Get("wheel_type") == "custom");
  CHECK(page.config.Get("wheel_ffb_damper") == "90");
  CHECK(rex::cvar::GetFlagByName("wheel_ffb_damper") == "90");
  CHECK_FALSE(page.screen->rows()[page.Row("DAMPING")].unapplied());
}

TEST_CASE("Leaving CUSTOM for a wheel type asks for a second apply", "[wheel]") {
  const std::string custom = "wheel_type = \"custom\"\nwheel_ffb_damper = 150\n";
  Page page(custom);
  CHECK(page.Value("WHEEL TYPE") == "CUSTOM");
  page.Adjust("WHEEL TYPE", NavCommand::kRight);
  CHECK(page.Value("WHEEL TYPE") == "DIRECT DRIVE");
  page.screen->Handle(NavCommand::kApply);
  CHECK(page.File() == custom);
  CHECK(page.screen->status().text == "THIS REPLACES YOUR CUSTOM VALUES: PRESS Y AGAIN");
  CHECK(page.screen->status().warning);
  page.screen->Handle(NavCommand::kApply);
  CHECK(page.config.Get("wheel_type") == "direct");
  CHECK(page.config.Get("wheel_ffb_damper") == "100");
  CHECK(rex::cvar::GetFlagByName("wheel_ffb_jolt") == "50");
  CHECK(page.screen->status().text == "SETTINGS SAVED AND APPLIED.");
}

TEST_CASE("The wheel type cycle skips CUSTOM", "[wheel]") {
  Page page("wheel_type = \"gear\"\n");
  page.Adjust("WHEEL TYPE", NavCommand::kRight);
  CHECK(page.Value("WHEEL TYPE") == "DIRECT DRIVE");
  page.Adjust("WHEEL TYPE", NavCommand::kLeft);
  CHECK(page.Value("WHEEL TYPE") == "GEAR");
}

TEST_CASE("STEERING LOCK stops at the wheel rotation and sets the steering scale", "[wheel]") {
  Page page("wheel_rotation = 900\n");
  page.Adjust("STEERING LOCK", NavCommand::kRight);
  CHECK(page.Value("STEERING LOCK") == "900 DEGREES");
  page.Adjust("STEERING LOCK", NavCommand::kLeft);
  page.screen->Handle(NavCommand::kApply);
  CHECK(page.Value("STEERING LOCK") == "890 DEGREES");
  CHECK(page.config.Get("wheel_steering_scale") == "101");
  CHECK(rex::cvar::GetFlagByName("wheel_steering_scale") == "101");
  CHECK_FALSE(page.config.Get("wheel_type"));
}

TEST_CASE("A row set by a launch option says so", "[wheel]") {
  Page page("");
  REQUIRE(rex::cvar::SetFlagFromCommandLine("wheel_ffb_spring", "90"));
  CHECK(page.screen->rows()[page.Row("SELF-CENTRING")].badge() ==
        "Set by a launch option for this session.");
  CHECK(page.screen->rows()[page.Row("DAMPING")].badge().empty());
}

TEST_CASE("Applying keeps a launch option the player did not change", "[wheel]") {
  Page page("");
  REQUIRE(rex::cvar::SetFlagFromCommandLine("wheel_ffb_spring", "90"));
  page.screen = ui::CreateWheelPage(page.config);
  CHECK(page.Value("SELF-CENTRING") == "90 %");
  page.Adjust("STRENGTH", NavCommand::kLeft);
  page.screen->Handle(NavCommand::kApply);
  CHECK(rex::cvar::GetFlagByName("wheel_ffb_spring") == "90");
  CHECK(rex::cvar::GetFlagByName("wheel_ffb_strength") == "80");
  CHECK(page.Value("SELF-CENTRING") == "90 %");
}

TEST_CASE("Applying keeps what the launcher saved while the game ran", "[wheel]") {
  Page page("wheel_type = \"custom\"\nwheel_ffb_jolt = 30\n");
  std::ofstream(page.path, std::ios::binary | std::ios::trunc)
      << "wheel_type = \"custom\"\nwheel_ffb_jolt = 70\nwheel_device = \"346e:0004\"\n";
  page.Adjust("STRENGTH", NavCommand::kLeft);
  page.screen->Handle(NavCommand::kApply);
  const std::string file = page.File();
  CHECK(file.find("wheel_ffb_jolt = 70") != std::string::npos);
  CHECK(file.find("wheel_device = \"346e:0004\"") != std::string::npos);
  CHECK(file.find("wheel_ffb_strength = 80") != std::string::npos);
}
