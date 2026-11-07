// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <rex/ui/virtual_key.h>

namespace fm4_static_recomp::config {
class HostConfig;
}

namespace fm4_static_recomp::input {

enum class PadControl {
  kA,
  kB,
  kX,
  kY,
  kLeftShoulder,
  kRightShoulder,
  kLeftTrigger,
  kRightTrigger,
  kLStickUp,
  kLStickDown,
  kLStickLeft,
  kLStickRight,
  kLStickPress,
  kRStickUp,
  kRStickDown,
  kRStickLeft,
  kRStickRight,
  kRStickPress,
  kDpadUp,
  kDpadDown,
  kDpadLeft,
  kDpadRight,
  kBack,
  kStart,
  kCount
};

// The keys of each pad control as the SDK keyboard driver reads them: its
// keybind_* syntax, alternatives separated by commas ("W,Up").
struct BindingSet {
  std::array<std::string, size_t(PadControl::kCount)> keys;
};

// Seconds from rest to full and back, for the smoothed keyboard driving.
struct RampRates {
  double steer_seconds;
  double steer_return_seconds;
  double pedal_seconds;
  double pedal_return_seconds;
  double look_seconds;
};

struct Controls {
  BindingSet menu, driving;
  RampRates ramp;
};

Controls DefaultControls();
// controls.toml: menu_<control> and driving_<control> (a, b, x, y,
// left_shoulder, right_shoulder, left_trigger, right_trigger, lstick_up,
// lstick_down, lstick_left, lstick_right, lstick_press, rstick_*, dpad_up,
// dpad_down, dpad_left, dpad_right, back, start) and ramp_steer_seconds,
// ramp_steer_return_seconds, ramp_pedal_seconds, ramp_pedal_return_seconds,
// ramp_look_seconds. A missing, empty or unreadable value keeps the default.
Controls LoadControls(const config::HostConfig& config);
// The same over any source of those values; `log` writes what LoadControls(config) logs.
using ControlLookup = std::function<std::optional<std::string>(std::string_view name)>;
Controls LoadControls(const ControlLookup& lookup, bool log);

// Parse a nonnegative ramp duration with the same rules in the loader and editor.
std::optional<double> ParseRampSeconds(std::string_view value);

// The control's name in controls.toml after menu_ / driving_ ("a", "lstick_up").
std::string_view ControlFileName(PadControl control);

// The keys of a value the game can use, as LoadControls keeps them ("W,Up"); empty when none.
std::string UsableKeys(std::string_view bind);

// A pressed key's name in the bind syntax ("W", "Return", "Shift"); empty for the function
// keys (the app's shortcuts) and the mouse buttons.
std::string BindKeyName(rex::ui::VirtualKey key);

// Hands the set to the keyboard driver in place of its keybind_* options.
void ApplyBindings(const BindingSet& set);

}  // namespace fm4_static_recomp::input
