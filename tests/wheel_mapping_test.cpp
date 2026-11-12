// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/input/xinputd_ffb.h>

#include "config/host_config.h"
#include "wheel/wheel_mapping.h"
#include "wheel/wheel_settings.h"

using namespace fm4_static_recomp::wheel;
namespace ffb = rex::input::xinputd_ffb;

namespace {

Device MakeDevice(uint32_t id, std::string name, uint16_t vendor, uint16_t product, int axes,
                  int buttons, int hats, bool wheel = false) {
  Device d;
  d.id = id;
  d.name = std::move(name);
  d.path = "path-" + std::to_string(id);
  d.vendor = vendor;
  d.product = product;
  d.wheel = wheel;
  d.axes.assign(axes, 0);
  d.rest = d.axes;
  d.buttons.assign(buttons, false);
  d.hats.assign(hats, 0);
  return d;
}

// A direct-drive base, separate pedals resting at -32768, and a shifter.
Snapshot Rig() {
  Snapshot s;
  s.devices.push_back(MakeDevice(1, "Wheel Base", 0x346e, 0x0006, 2, 12, 1, true));
  Device pedals = MakeDevice(2, "SimPedals", 0x1dd2, 0x2000, 3, 0, 0);
  pedals.axes = pedals.rest = {-32768, -32768, -32768};
  s.devices.push_back(pedals);
  s.devices.push_back(MakeDevice(3, "Shifter", 0x0079, 0x0006, 0, 8, 0));
  return s;
}

ffb::Effect Constant(int magnitude, int gain = 255) {
  ffb::Effect e;
  e.id = 5;
  e.type = ffb::kConstant;
  e.gain = gain;
  e.magnitude = magnitude;
  return e;
}

}  // namespace

TEST_CASE("Wheel devices are found and named apart", "[wheel]") {
  Snapshot s = Rig();
  REQUIRE(FindDevice(s.devices, "1dd2:2000") == &s.devices[1]);
  REQUIRE(FindDevice(s.devices, "simped") == &s.devices[1]);
  REQUIRE(FindDevice(s.devices, "path:path-3") == &s.devices[2]);
  REQUIRE(FindDevice(s.devices, "") == nullptr);
  CHECK(DeviceMatch(s.devices[0], s.devices) == "346e:0006");
  // Two identical bases (a Fanatec base's two HID interfaces) are told apart by path.
  s.devices.push_back(MakeDevice(4, "Wheel Base", 0x346e, 0x0006, 2, 12, 1, true));
  CHECK(DeviceMatch(s.devices[0], s.devices) == "path:path-1");
}

TEST_CASE("The wheel base is the device named, else the first SDL wheel", "[wheel]") {
  const Snapshot s = Rig();
  Settings settings;
  CHECK(Composer(settings).Base(s) == &s.devices[0]);
  settings.device = "Shifter";
  CHECK(Composer(settings).Base(s) == &s.devices[2]);
  settings.device = "not plugged in";
  CHECK(Composer(settings).Base(s) == nullptr);
}

TEST_CASE("Steering follows the rotation the game's wheel turns", "[wheel]") {
  Settings s;
  CHECK(SteeringScale(s) == 100);
  s.game_rotation = 360;  // 900 on 360: full lock at 360 degrees
  CHECK(SteeringScale(s) == 250);
  s.steering_scale = 50;
  CHECK(SteeringScale(s) == 125);

  Snapshot snap = Rig();
  snap.devices[0].axes[0] = 16384;
  Settings plain;
  CHECK(int16_t(Composer(plain).Compose(snap).thumb_lx) == 16384);
  plain.steering_invert = true;
  CHECK(int16_t(Composer(plain).Compose(snap).thumb_lx) == -16385);
  Settings quick;
  quick.game_rotation = 450;  // half the wheel's 900: twice as quick, clipped at full lock
  CHECK(int16_t(Composer(quick).Compose(snap).thumb_lx) == 32767);
}

TEST_CASE("Pedals on a separate device reach the triggers and the clutch button", "[wheel]") {
  Snapshot snap = Rig();
  Settings s;
  s.throttle = "SimPedals|axis0";
  s.brake = "1dd2:2000|axis1";
  s.clutch = "SimPedals|axis2";
  s.pedal_deadzone = 0;
  snap.devices[1].axes = {32767, 0, 32767};
  const Composer composer(s);
  const auto pad = composer.Compose(snap);
  CHECK(pad.right_trigger == 255);
  CHECK(pad.left_trigger == 127);
  // FM4's clutch is a button: B by default, past half travel.
  CHECK((uint16_t(pad.buttons) & rex::input::X_INPUT_GAMEPAD_B) != 0);
  CHECK(composer.Pedal(snap, 2) == 255);
  // A pedal that rests at the top travels downwards.
  snap.devices[1].rest[0] = 32767;
  snap.devices[1].axes[0] = 32767;
  CHECK(Composer(s).Compose(snap).right_trigger == 0);
}

TEST_CASE("A reversed pedal reads its minimum as its maximum", "[wheel]") {
  Snapshot snap = Rig();  // pedals resting at -32768
  Settings s;
  s.pedal_deadzone = 0;
  s.throttle = "SimPedals|axis0";
  s.brake = "SimPedals|-axis1";
  auto pad = Composer(s).Compose(snap);
  CHECK(pad.right_trigger == 0);
  CHECK(pad.left_trigger == 255);
  snap.devices[1].axes[1] = 32767;
  CHECK(Composer(s).Compose(snap).left_trigger == 0);
  // "+" forces normal travel even for a pedal resting at the top.
  snap.devices[1].rest[0] = snap.devices[1].axes[0] = 32767;
  s.throttle = "SimPedals|+axis0";
  CHECK(Composer(s).Compose(snap).right_trigger == 255);
}

TEST_CASE("Buttons, hats and H-shifter gates bind across devices", "[wheel]") {
  Snapshot snap = Rig();
  Settings s;
  s.buttons = "A=3,START=Shifter|7,GEAR2=Shifter|1,UP=h0.up";
  snap.devices[0].buttons[3] = true;
  snap.devices[2].buttons[1] = true;
  snap.devices[0].hats[0] = 1;  // SDL_HAT_UP
  const Composer composer(s);
  REQUIRE(composer.shifter());
  const auto pad = composer.Compose(snap);
  const uint16_t buttons = pad.buttons;
  CHECK((buttons & rex::input::X_INPUT_GAMEPAD_A) != 0);
  CHECK((buttons & rex::input::X_INPUT_GAMEPAD_START) == 0);
  CHECK((buttons & rex::input::X_INPUT_GAMEPAD_DPAD_UP) != 0);
  CHECK(int16_t(pad.thumb_rx) == 2);
}

TEST_CASE("Clutch buttons parse as pad names", "[wheel]") {
  CHECK(ButtonsFromNames("B") == rex::input::X_INPUT_GAMEPAD_B);
  CHECK(ButtonsFromNames("A+LB") ==
        (rex::input::X_INPUT_GAMEPAD_A | rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER));
  CHECK(ButtonsFromNames("") == 0);
}

TEST_CASE("FM4's effects scale from +/-127 to SDL's full range", "[wheel]") {
  Settings s;
  s.ffb_strength = 100;
  CHECK(ConstantLevel(Constant(127), s) == 32767);
  CHECK(ConstantLevel(Constant(-127), s) == -32767);
  s.ffb_strength = 25;
  CHECK(ConstantLevel(Constant(127), s) == 8191);
  // Above 100 % small forces grow and the strongest clip at full scale.
  s.ffb_strength = 100;
  const int half = ConstantLevel(Constant(32), s);
  s.ffb_strength = 200;
  CHECK(ConstantLevel(Constant(32), s) == half * 2);
  CHECK(ConstantLevel(Constant(127), s) == 32767);
  s.ffb_strength = 100;
  s.ffb_invert = true;
  CHECK(ConstantLevel(Constant(127), s) == -32767);
  CHECK(ConstantLevel(Constant(0), s) == 0);
}

TEST_CASE("Condition effects take their own share", "[wheel]") {
  Settings s;
  s.ffb_strength = 100;
  ffb::Effect spring;
  spring.type = ffb::kSpring;
  spring.gain = 255;
  spring.positive_coefficient = spring.negative_coefficient = 127;
  spring.positive_saturation = spring.negative_saturation = 255;
  auto c = ConditionValues(spring, s);
  CHECK(c.right_coeff == 32767);
  CHECK(c.right_sat == 65535);
  s.ffb_spring = 50;
  c = ConditionValues(spring, s);
  CHECK(c.right_coeff == 16383);
  s.ffb_damper = 0;  // another effect's share does not touch the spring
  CHECK(ConditionValues(spring, s).left_coeff == 16383);
}

TEST_CASE("The test force and the rumble", "[wheel]") {
  Settings s;
  s.ffb_strength = 100;
  CHECK(TestForceLevel(s) == 16384);
  s.ffb_strength = 200;
  CHECK(TestForceLevel(s) == 32767);
  s.ffb_invert = true;
  CHECK(TestForceLevel(s) == -32767);

  Settings r;
  r.ffb_rumble = 100;
  CHECK(RumbleMagnitude(65535, 0, r) == 32767);
  r.ffb_rumble_road = 0;
  CHECK(RumbleMagnitude(65535, 0, r) == 0);
  CHECK(RumbleMagnitude(0, 65535, r) == 32767);
  CHECK(ParseFfbMode("rumble") == FfbMode::kRumble);
  CHECK(ParseFfbMode("off") == FfbMode::kOff);
  CHECK(ParseFfbMode("on") == FfbMode::kEffects);
}

TEST_CASE("wheel.toml reaches the settings", "[wheel]") {
  const auto path = std::filesystem::temp_directory_path() / "fm4_wheel_settings_test.toml";
  REQUIRE(fm4_static_recomp::config::WriteAtomically(
      path, "fm4_wheel_enabled = true\nfm4_wheel_throttle = \"SimPedals|-axis1\"\n"
            "fm4_wheel_ffb_strength = 60\n"));
  fm4_static_recomp::config::HostConfig config(path);
  REQUIRE(config.Load());
  ApplySavedWheelSettings(config);
  const Settings s = ReadSettings();
  CHECK(s.enabled);
  CHECK(s.throttle == "SimPedals|-axis1");
  CHECK(s.ffb_strength == 60);
  rex::cvar::SetFlagByName("fm4_wheel_enabled", "false");
  rex::cvar::SetFlagByName("fm4_wheel_throttle", "");
  rex::cvar::SetFlagByName("fm4_wheel_ffb_strength", "25");
  std::filesystem::remove(path);
}
