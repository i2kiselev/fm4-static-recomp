// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
#include "wheel/wheel_settings.h"

#include <string_view>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "config/host_config.h"

#define WHEEL_GROUP "Forza Motorsport 4 Wheel"

REXCVAR_DEFINE_BOOL(fm4_wheel_enabled, false, WHEEL_GROUP,
                    "Use the racing wheel bindings below as player one's controller");
REXCVAR_DEFINE_STRING(fm4_wheel_device, "", WHEEL_GROUP,
                      "Wheel base: vvvv:pppp, part of its name, or path:<SDL path>; empty for the "
                      "first device SDL types as a wheel");
REXCVAR_DEFINE_INT32(fm4_wheel_steering_axis, 0, WHEEL_GROUP, "Steering axis on the wheel base");
REXCVAR_DEFINE_BOOL(fm4_wheel_steering_invert, false, WHEEL_GROUP, "Reverse the steering axis");
REXCVAR_DEFINE_INT32(fm4_wheel_steering_scale, 100, WHEEL_GROUP,
                     "Steering sensitivity in percent; 200 reaches full lock at half the wheel's "
                     "rotation");
REXCVAR_DEFINE_INT32(fm4_wheel_steering_deadzone, 0, WHEEL_GROUP,
                     "Steering dead zone in percent of half travel");
REXCVAR_DEFINE_INT32(fm4_wheel_rotation, 900, WHEEL_GROUP,
                     "The rotation set in the wheel's software, degrees lock to lock");
REXCVAR_DEFINE_INT32(fm4_wheel_game_rotation, 0, WHEEL_GROUP,
                     "The in-game steering wheel's rotation lock to lock in degrees, so it turns "
                     "as far as the real one; 0 maps the wheel's full rotation to full lock");
REXCVAR_DEFINE_STRING(fm4_wheel_throttle, "", WHEEL_GROUP, "Throttle axis, [device|][+|-]axisN");
REXCVAR_DEFINE_STRING(fm4_wheel_brake, "", WHEEL_GROUP, "Brake axis, [device|][+|-]axisN");
REXCVAR_DEFINE_STRING(fm4_wheel_clutch, "", WHEEL_GROUP, "Clutch axis, [device|][+|-]axisN");
REXCVAR_DEFINE_STRING(fm4_wheel_clutch_button, "B", WHEEL_GROUP,
                      "Pad buttons the clutch pedal presses past half travel (FM4's clutch is a "
                      "button)");
REXCVAR_DEFINE_STRING(fm4_wheel_buttons, "UP=h0.up,DOWN=h0.down,LEFT=h0.left,RIGHT=h0.right",
                      WHEEL_GROUP,
                      "Button bindings, NAME=[device|]input,...: N, hN.up, [+|-]axisN; NAME a pad "
                      "control or GEAR1-GEAR7/REVERSE");
REXCVAR_DEFINE_INT32(fm4_wheel_pedal_deadzone, 3, WHEEL_GROUP,
                     "Pedal dead zone at rest in percent, for pedals without their own");
REXCVAR_DEFINE_INT32(fm4_wheel_throttle_deadzone_bottom, -1, WHEEL_GROUP,
                     "Throttle dead zone at rest; -1 uses fm4_wheel_pedal_deadzone");
REXCVAR_DEFINE_INT32(fm4_wheel_throttle_deadzone_top, 0, WHEEL_GROUP,
                     "Throttle travel at the top that already reads fully pressed");
REXCVAR_DEFINE_INT32(fm4_wheel_brake_deadzone_bottom, -1, WHEEL_GROUP,
                     "Brake dead zone at rest; -1 uses fm4_wheel_pedal_deadzone");
REXCVAR_DEFINE_INT32(fm4_wheel_brake_deadzone_top, 0, WHEEL_GROUP,
                     "Brake travel at the top that already reads fully pressed");
REXCVAR_DEFINE_INT32(fm4_wheel_clutch_deadzone_bottom, -1, WHEEL_GROUP,
                     "Clutch dead zone at rest; -1 uses fm4_wheel_pedal_deadzone");
REXCVAR_DEFINE_INT32(fm4_wheel_clutch_deadzone_top, 0, WHEEL_GROUP,
                     "Clutch travel at the top that already reads fully pressed");
REXCVAR_DEFINE_STRING(fm4_wheel_ffb_device, "", WHEEL_GROUP,
                      "Device that receives force feedback; empty for the wheel base");
REXCVAR_DEFINE_STRING(fm4_wheel_ffb_mode, "on", WHEEL_GROUP,
                      "on (the game's own wheel effects), rumble (vibration only) or off")
    .allowed({"on", "rumble", "off"});
REXCVAR_DEFINE_INT32(fm4_wheel_ffb_strength, 25, WHEEL_GROUP,
                     "Overall force feedback strength in percent (0-200; above 100 the strongest "
                     "forces clip); start low on a direct-drive base");
REXCVAR_DEFINE_INT32(fm4_wheel_ffb_min_force, 0, WHEEL_GROUP,
                     "Lift any non-zero force to at least this percent, for geared or belted "
                     "wheels");
REXCVAR_DEFINE_INT32(fm4_wheel_ffb_spring, 100, WHEEL_GROUP,
                     "Strength of the game's centring spring, percent (0-200)");
REXCVAR_DEFINE_INT32(fm4_wheel_ffb_damper, 100, WHEEL_GROUP,
                     "Strength of the game's damper, percent (0-200)");
REXCVAR_DEFINE_INT32(fm4_wheel_ffb_friction, 100, WHEEL_GROUP,
                     "Strength of the game's friction, percent (0-200)");
REXCVAR_DEFINE_INT32(fm4_wheel_ffb_inertia, 100, WHEEL_GROUP,
                     "Strength of the game's inertia, percent (0-200)");
REXCVAR_DEFINE_INT32(fm4_wheel_ffb_rumble, 50, WHEEL_GROUP,
                     "Strength of the game's rumble played as a vibration, percent");
REXCVAR_DEFINE_INT32(fm4_wheel_ffb_rumble_engine, 100, WHEEL_GROUP,
                     "Share of the rumble from the right (engine) motor, percent");
REXCVAR_DEFINE_INT32(fm4_wheel_ffb_rumble_road, 100, WHEEL_GROUP,
                     "Share of the rumble from the left (road) motor, percent");
REXCVAR_DEFINE_BOOL(fm4_wheel_ffb_invert, false, WHEEL_GROUP, "Reverse the force direction");
REXCVAR_DEFINE_BOOL(fm4_wheel_ffb_ignore_driving, false, WHEEL_GROUP,
                    "Diagnostics: play the forces whatever the game's driving flag says");
REXCVAR_DEFINE_BOOL(fm4_wheel_ffb_periodic_restart, false, WHEEL_GROUP,
                    "Also start every running effect again once a second, for a driver that stops "
                    "effects on its own; each effect is already started again after every update");
REXCVAR_DEFINE_BOOL(fm4_wheel_log_inputs, false, WHEEL_GROUP,
                    "Log raw axis, button and hat changes of every device, to find input numbers");

namespace fm4_static_recomp::wheel {

Settings ReadSettings() {
  Settings s;
  s.enabled = REXCVAR_GET(fm4_wheel_enabled);
  s.device = REXCVAR_GET(fm4_wheel_device);
  s.steering_axis = REXCVAR_GET(fm4_wheel_steering_axis);
  s.steering_invert = REXCVAR_GET(fm4_wheel_steering_invert);
  s.steering_scale = REXCVAR_GET(fm4_wheel_steering_scale);
  s.steering_deadzone = REXCVAR_GET(fm4_wheel_steering_deadzone);
  s.wheel_rotation = REXCVAR_GET(fm4_wheel_rotation);
  s.game_rotation = REXCVAR_GET(fm4_wheel_game_rotation);
  s.throttle = REXCVAR_GET(fm4_wheel_throttle);
  s.brake = REXCVAR_GET(fm4_wheel_brake);
  s.clutch = REXCVAR_GET(fm4_wheel_clutch);
  s.clutch_button = REXCVAR_GET(fm4_wheel_clutch_button);
  s.buttons = REXCVAR_GET(fm4_wheel_buttons);
  s.pedal_deadzone = REXCVAR_GET(fm4_wheel_pedal_deadzone);
  s.deadzone_bottom = {{REXCVAR_GET(fm4_wheel_throttle_deadzone_bottom),
                        REXCVAR_GET(fm4_wheel_brake_deadzone_bottom),
                        REXCVAR_GET(fm4_wheel_clutch_deadzone_bottom)}};
  s.deadzone_top = {{REXCVAR_GET(fm4_wheel_throttle_deadzone_top),
                     REXCVAR_GET(fm4_wheel_brake_deadzone_top),
                     REXCVAR_GET(fm4_wheel_clutch_deadzone_top)}};
  s.ffb_device = REXCVAR_GET(fm4_wheel_ffb_device);
  s.ffb_mode = REXCVAR_GET(fm4_wheel_ffb_mode);
  s.ffb_strength = REXCVAR_GET(fm4_wheel_ffb_strength);
  s.ffb_min_force = REXCVAR_GET(fm4_wheel_ffb_min_force);
  s.ffb_spring = REXCVAR_GET(fm4_wheel_ffb_spring);
  s.ffb_damper = REXCVAR_GET(fm4_wheel_ffb_damper);
  s.ffb_friction = REXCVAR_GET(fm4_wheel_ffb_friction);
  s.ffb_inertia = REXCVAR_GET(fm4_wheel_ffb_inertia);
  s.ffb_rumble = REXCVAR_GET(fm4_wheel_ffb_rumble);
  s.ffb_rumble_engine = REXCVAR_GET(fm4_wheel_ffb_rumble_engine);
  s.ffb_rumble_road = REXCVAR_GET(fm4_wheel_ffb_rumble_road);
  s.ffb_invert = REXCVAR_GET(fm4_wheel_ffb_invert);
  s.ffb_ignore_driving = REXCVAR_GET(fm4_wheel_ffb_ignore_driving);
  s.ffb_periodic_restart = REXCVAR_GET(fm4_wheel_ffb_periodic_restart);
  s.log_inputs = REXCVAR_GET(fm4_wheel_log_inputs);
  return s;
}

void ApplySavedWheelSettings(const config::HostConfig& config) {
  static constexpr std::string_view kNames[] = {
      "fm4_wheel_enabled",
      "fm4_wheel_device",
      "fm4_wheel_steering_axis",
      "fm4_wheel_steering_invert",
      "fm4_wheel_steering_scale",
      "fm4_wheel_steering_deadzone",
      "fm4_wheel_rotation",
      "fm4_wheel_game_rotation",
      "fm4_wheel_throttle",
      "fm4_wheel_brake",
      "fm4_wheel_clutch",
      "fm4_wheel_clutch_button",
      "fm4_wheel_buttons",
      "fm4_wheel_pedal_deadzone",
      "fm4_wheel_throttle_deadzone_bottom",
      "fm4_wheel_throttle_deadzone_top",
      "fm4_wheel_brake_deadzone_bottom",
      "fm4_wheel_brake_deadzone_top",
      "fm4_wheel_clutch_deadzone_bottom",
      "fm4_wheel_clutch_deadzone_top",
      "fm4_wheel_ffb_device",
      "fm4_wheel_ffb_mode",
      "fm4_wheel_ffb_strength",
      "fm4_wheel_ffb_min_force",
      "fm4_wheel_ffb_spring",
      "fm4_wheel_ffb_damper",
      "fm4_wheel_ffb_friction",
      "fm4_wheel_ffb_inertia",
      "fm4_wheel_ffb_rumble",
      "fm4_wheel_ffb_rumble_engine",
      "fm4_wheel_ffb_rumble_road",
      "fm4_wheel_ffb_invert",
      "fm4_wheel_ffb_ignore_driving",
      "fm4_wheel_ffb_periodic_restart",
      "fm4_wheel_log_inputs",
  };
  for (const std::string_view name : kNames) {
    const auto value = config.Get(name);
    if (!value) {
      continue;
    }
    if (rex::cvar::GetFlagSource(name) == rex::cvar::Source::kCommandLine) {
      REXLOG_INFO("Wheel settings: {} comes from the command line, not {}", name,
                  config.path().string());
    } else if (!rex::cvar::SetFlagByName(name, *value)) {
      REXLOG_WARN("Wheel settings: ignored {} = {} in {}", name, *value, config.path().string());
    }
  }
}

}  // namespace fm4_static_recomp::wheel
