// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <array>
#include <string>

namespace fm4_static_recomp::config {
class HostConfig;
}

namespace fm4_static_recomp::wheel {

// The fm4_wheel_* settings (config/wheel.toml, written by the launcher's WHEEL panel). Named
// apart from the SDK's own wheel_* driver, which stays off: a cvar registered twice keeps only
// the first registration's value.
struct Settings {
  bool enabled = false;
  // The wheel base: steering and, by default, force feedback. "vvvv:pppp", part of its name or
  // "path:<SDL path>"; empty for the first device SDL types as a wheel.
  std::string device;
  int steering_axis = 0;
  bool steering_invert = false;
  int steering_scale = 100, steering_deadzone = 0;
  // Degrees lock to lock: the rotation set in the wheel's software and the in-game steering
  // wheel's. With game_rotation set, full lock comes when the real wheel has turned that far,
  // so the cockpit wheel follows it; 0 maps the wheel's full rotation to full lock.
  int wheel_rotation = 900, game_rotation = 0;
  // Pedal axes, "[device|][+|-]axisN".
  std::string throttle, brake, clutch;
  // "NAME=[device|]input,...", see the SDK's wheel_buttons (docs/RACING_WHEELS.md).
  std::string buttons = "UP=h0.up,DOWN=h0.down,LEFT=h0.left,RIGHT=h0.right";
  // FM4's clutch is a button: the clutch pedal past half travel presses these.
  std::string clutch_button = "B";
  int pedal_deadzone = 3;
  // Throttle, brake, clutch: dead zone at rest (-1 uses pedal_deadzone) and at full travel.
  std::array<int, 3> deadzone_bottom{{-1, -1, -1}}, deadzone_top{{0, 0, 0}};

  std::string ffb_device;  // empty: the wheel base
  std::string ffb_mode = "on";  // on (the game's own effects), rumble, off
  int ffb_strength = 25, ffb_min_force = 0;
  int ffb_spring = 100, ffb_damper = 100, ffb_friction = 100, ffb_inertia = 100;
  int ffb_rumble = 50, ffb_rumble_engine = 100, ffb_rumble_road = 100;
  bool ffb_invert = false;
  // Diagnostics: play the forces whatever the game's driving flag says.
  bool ffb_ignore_driving = false;
  // Start every running effect again once a second, besides the start after each update (which
  // is what Thrustmaster's driver needs); a fallback for a driver that stops effects on its own.
  bool ffb_periodic_restart = false;
  bool log_inputs = false;
};

Settings ReadSettings();

// Sets every fm4_wheel_* key of `config` that the command line did not set.
void ApplySavedWheelSettings(const config::HostConfig& config);

}  // namespace fm4_static_recomp::wheel
