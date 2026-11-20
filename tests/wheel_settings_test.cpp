// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <rex/cvar.h>

#include "config/host_config.h"
#include "wheel_settings.h"

namespace {

namespace config = fm4_static_recomp::config;
using namespace fm4_static_recomp;

struct WheelFile {
  explicit WheelFile(std::string_view text = "")
      : path(std::filesystem::temp_directory_path() / "fm4_wheel_settings_test.toml"),
        config(path) {
    ResetCvars();
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    REQUIRE(config.Load());
  }
  ~WheelFile() {
    std::filesystem::remove(path);
    ResetCvars();
  }
  static void ResetCvars() {
    for (const WheelRow& row : WheelRows()) {
      rex::cvar::ResetToDefault(row.key);
    }
    rex::cvar::ResetToDefault("wheel_enabled");
    rex::cvar::ResetToDefault("wheel_steering_scale");
  }

  std::filesystem::path path;
  config::HostConfig config;
};

std::string ReadText(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

size_t Count(const std::string& text, std::string_view what) {
  size_t count = 0;
  for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
    ++count;
  }
  return count;
}

}  // namespace

TEST_CASE("A wheel type writes its values", "[wheel]") {
  WheelFile file;
  SetWheelType(file.config, "belt");
  CHECK(file.config.Get("wheel_ffb_damper") == "80");
  CHECK(WheelTypeOf(file.config) == "belt");
}

TEST_CASE("Changing a wheel type's value makes it custom; strength does not", "[wheel]") {
  WheelFile file;
  SECTION("advanced") {
    SetWheelType(file.config, "belt");
    SetWheelValue(file.config, "wheel_ffb_damper", 150);
    CHECK(WheelTypeOf(file.config) == "custom");
  }
  SECTION("basic") {
    SetWheelType(file.config, "gear");
    SetWheelValue(file.config, "wheel_ffb_strength", 70);
    CHECK(WheelTypeOf(file.config) == "gear");
    CHECK(file.config.Get("wheel_ffb_strength") == "70");
  }
}

TEST_CASE("A value is clamped to its row", "[wheel]") {
  WheelFile file;
  SetWheelValue(file.config, "wheel_ffb_jolt", 400);
  CHECK(file.config.Get("wheel_ffb_jolt") == "100");
}

TEST_CASE("An unknown wheel type with a type's value is custom", "[wheel]") {
  WheelFile unknown("wheel_type = \"banana\"\nwheel_ffb_spring = 120\n");
  CHECK(WheelTypeOf(unknown.config) == "custom");
}

TEST_CASE("No wheel type and no type's value is direct drive, with its values", "[wheel]") {
  WheelFile file("wheel_ffb_strength = 70\n");
  CHECK(WheelTypeOf(file.config) == "direct");
  CHECK(WheelValue(file.config, "wheel_ffb_jolt") == 50);
  CHECK(WheelValue(file.config, "wheel_ffb_strength") == 70);
}

TEST_CASE("Steering lock is the wheel rotation over the steering scale", "[wheel]") {
  WheelFile file("wheel_rotation = 900\n");
  SetSteeringLock(file.config, 540);
  CHECK(file.config.Get("wheel_steering_scale") == "167");
  CHECK(SteeringLockDegrees(file.config) == 540);
  SetSteeringLock(file.config, 2000);
  CHECK(file.config.Get("wheel_steering_scale") == "100");
}

TEST_CASE("Steering lock stops where the steering scale tops out", "[wheel]") {
  CHECK(MinSteeringLock(900) == 180);
  CHECK(MinSteeringLock(2520) == 260);
  WheelFile file("wheel_rotation = 2520\n");
  SetSteeringLock(file.config, 180);
  CHECK(file.config.Get("wheel_steering_scale") == "969");
  CHECK(SteeringLockDegrees(file.config) == 260);
}

TEST_CASE("The wheel update holds forces outside driving, on a wheel only", "[wheel]") {
  CHECK_FALSE(WheelForcesHeld(false, 1, 0));
  CHECK_FALSE(WheelForcesHeld(true, 0, 0));
  CHECK(WheelForcesHeld(true, 1, 0) == true);
  CHECK(WheelForcesHeld(true, 1, 1) == false);
}

TEST_CASE("Saved wheel settings apply below a launch option", "[wheel]") {
  SECTION("from the file") {
    WheelFile file("wheel_ffb_spring = 120\n");
    ApplySavedWheelSettings(file.config);
    CHECK(rex::cvar::GetFlagByName("wheel_ffb_spring") == "120");
  }
  SECTION("command line wins") {
    WheelFile file("wheel_ffb_spring = 120\n");
    REQUIRE(rex::cvar::SetFlagFromCommandLine("wheel_ffb_spring", "90"));
    ApplySavedWheelSettings(file.config);
    CHECK(rex::cvar::GetFlagByName("wheel_ffb_spring") == "90");
  }
  SECTION("a wheel on fills in the wheel type's values") {
    WheelFile file("wheel_enabled = true\nwheel_type = \"belt\"\n");
    ApplySavedWheelSettings(file.config);
    CHECK(rex::cvar::GetFlagByName("wheel_ffb_damper") == "80");
  }
  SECTION("an out-of-range value runs clamped") {
    WheelFile file("wheel_ffb_jolt = 400\n");
    ApplySavedWheelSettings(file.config);
    CHECK(rex::cvar::GetFlagByName("wheel_ffb_jolt") == "100");
  }
  SECTION("a garbled value runs the wheel type's") {
    WheelFile file("wheel_enabled = true\nwheel_type = \"direct\"\nwheel_ffb_jolt = abc\n");
    ApplySavedWheelSettings(file.config);
    CHECK(rex::cvar::GetFlagByName("wheel_ffb_jolt") == "50");
  }
  SECTION("no wheel leaves the rest alone") {
    WheelFile file("wheel_type = \"belt\"\n");
    ApplySavedWheelSettings(file.config);
    CHECK(rex::cvar::GetFlagByName("wheel_ffb_damper") == "100");
  }
}

TEST_CASE("The wheel schema lists the rows, the types and the expert settings", "[wheel]") {
  const auto path = std::filesystem::temp_directory_path() / "fm4_wheel_schema_test.json";
  REQUIRE(WriteWheelSchema(path));
  const std::string schema = ReadText(path);
  std::filesystem::remove(path);
  REQUIRE(schema.starts_with("{\"version\":1,\"rows\":["));
  const size_t types = schema.find("],\"types\":[");
  const size_t expert = schema.find("],\"expert\":[");
  REQUIRE(types != std::string::npos);
  REQUIRE(expert != std::string::npos);
  CHECK(Count(schema.substr(0, types), "{\"key\":") == 9);
  CHECK(Count(schema.substr(types, expert - types), "{\"id\":") == 3);
  CHECK(schema.find("{\"key\":\"wheel_ffb_mode\"", expert) != std::string::npos);
  CHECK(schema.find("{\"key\":\"wheel_ffb_spring\"", expert) == std::string::npos);
  const size_t magnitude = schema.find("{\"key\":\"wheel_ffb_magnitude_max\"", expert);
  REQUIRE(magnitude != std::string::npos);
  CHECK(std::string_view(schema).substr(schema.find("\"default\":", magnitude))
            .starts_with("\"default\":\"127\""));
  CHECK(schema.ends_with("]}\n"));
}

TEST_CASE("A device path the launcher wrote reads back whole", "[wheel]") {
  // TOML basic strings: the launcher doubles each backslash; '#' inside the quotes is not a comment.
  WheelFile file(
      "wheel_device = \"path:\\\\?\\HID#VID_346E&PID_0004#7&1\" # the R5\n"
      "wheel_buttons = \"RB=path:\\\\?\\HID#a|13,UP=h0.up\"\n"
      "wheel_name = \"say \\\"hi\\\"\"\n"
      "wheel_ffb_jolt = 40 # comment\n");
  CHECK(file.config.Get("wheel_device") == R"(path:\?\HID#VID_346E&PID_0004#7&1)");
  CHECK(file.config.Get("wheel_buttons") == R"(RB=path:\?\HID#a|13,UP=h0.up)");
  CHECK(file.config.Get("wheel_name") == R"(say "hi")");
  CHECK(file.config.Get("wheel_ffb_jolt") == "40");
}
