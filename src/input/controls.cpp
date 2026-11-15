// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "input/controls.h"

#include <algorithm>
#include <cmath>
#include <charconv>
#include <memory>
#include <string_view>
#include <vector>

#if defined(__APPLE__)
#include <cerrno>
#include <cstdlib>
#include <xlocale.h>
#endif

#include <rex/input/mnk/mnk_bind.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>

#include "config/host_config.h"

namespace fm4_static_recomp::input {
namespace {

struct ControlNames {
  const char* file;    // controls.toml name after menu_ / driving_
  const char* option;  // the SDK keyboard driver's option
};

constexpr std::array<ControlNames, size_t(PadControl::kCount)> kNames = {{
    {"a", "keybind_a"},
    {"b", "keybind_b"},
    {"x", "keybind_x"},
    {"y", "keybind_y"},
    {"left_shoulder", "keybind_left_shoulder"},
    {"right_shoulder", "keybind_right_shoulder"},
    {"left_trigger", "keybind_left_trigger"},
    {"right_trigger", "keybind_right_trigger"},
    {"lstick_up", "keybind_lstick_up"},
    {"lstick_down", "keybind_lstick_down"},
    {"lstick_left", "keybind_lstick_left"},
    {"lstick_right", "keybind_lstick_right"},
    {"lstick_press", "keybind_lstick_press"},
    {"rstick_up", "keybind_rstick_up"},
    {"rstick_down", "keybind_rstick_down"},
    {"rstick_left", "keybind_rstick_left"},
    {"rstick_right", "keybind_rstick_right"},
    {"rstick_press", "keybind_rstick_press"},
    {"dpad_up", "keybind_dpad_up"},
    {"dpad_down", "keybind_dpad_down"},
    {"dpad_left", "keybind_dpad_left"},
    {"dpad_right", "keybind_dpad_right"},
    {"back", "keybind_back"},
    {"start", "keybind_start"},
}};

// The tokens of a bind the keyboard driver can match ("Return", "Shift+W"); the
// others are logged and left out.
std::string KnownKeys(std::string_view name, std::string_view bind, bool log) {
  std::string kept;
  while (!bind.empty()) {
    const size_t comma = bind.find(',');
    std::string_view token = bind.substr(0, comma);
    bind = comma == std::string_view::npos ? std::string_view() : bind.substr(comma + 1);
    while (token.starts_with(' '))
      token.remove_prefix(1);
    while (token.ends_with(' '))
      token.remove_suffix(1);
    std::string_view key = token;
    bool known = true;
    for (size_t plus = key.find('+'); plus != std::string_view::npos && plus > 0;
         plus = key.find('+')) {
      const std::string_view modifier = key.substr(0, plus);
      known &=
          modifier == "Shift" || modifier == "Ctrl" || modifier == "Control" || modifier == "Alt";
      key.remove_prefix(plus + 1);
    }
    if (known && rex::ui::ParseVirtualKey(key) != rex::ui::VirtualKey::kNone) {
      kept += kept.empty() ? "" : ",";
      kept += token;
    } else if (!token.empty() && log) {
      REXLOG_WARN("Controls: {} names a key the keyboard does not have: '{}'", name, token);
    }
  }
  return kept;
}

// No host bind uses these.
constexpr std::array<std::string_view, 25> kSpareKeys = {
    "F", "Z", "X", "B", "N", "M", "Y", "U", "O", "H", "V", "R", "T",
    "G", "J", "K", "L", "I", "P", "1", "2", "3", "4", "5", "6"};

std::vector<std::string_view> Tokens(std::string_view bind) {
  std::vector<std::string_view> tokens;
  while (!bind.empty()) {
    const size_t comma = bind.find(',');
    tokens.push_back(bind.substr(0, comma));
    bind = comma == std::string_view::npos ? std::string_view() : bind.substr(comma + 1);
  }
  return tokens;
}

// With mnk_exact_modifiers off a plain bind fires under any modifiers, so "Ctrl+C" and "C"
// press the same key.
std::string_view BareKey(std::string_view token) {
  const size_t plus = token.rfind('+');
  return plus == std::string_view::npos || plus + 1 == token.size() ? token
                                                                     : token.substr(plus + 1);
}

bool Binds(std::string_view bind, std::string_view key) {
  return std::ranges::any_of(Tokens(bind), [&](std::string_view token) {
    return BareKey(token) == BareKey(key);
  });
}

bool BoundInSet(const BindingSet& set, std::string_view key) {
  return std::ranges::any_of(set.keys, [&](const std::string& bind) { return Binds(bind, key); });
}

// One key doing two things surprises the player, and every control must keep a key: a
// player's key leaves the default controls, and a control left with none takes the old key of
// the line that took its key, else a spare key, else shares its default as a last resort.
void LoadSet(const ControlLookup& lookup, bool log, std::string_view prefix, BindingSet& set) {
  const BindingSet defaults = set;
  std::array<bool, size_t(PadControl::kCount)> player{};
  const auto name_of = [&](size_t i) { return std::string(prefix) + kNames[i].file; };
  const auto taker_of = [&](std::string_view key) {
    size_t t = 0;
    while (t < kNames.size() && !(player[t] && Binds(set.keys[t], key)))
      ++t;
    return t;
  };
  for (size_t i = 0; i < kNames.size(); ++i) {
    const std::string name = name_of(i);
    if (auto value = lookup(name)) {
      if (std::string keys = KnownKeys(name, *value, log); !keys.empty()) {
        for (const std::string_view key : Tokens(keys)) {
          if (const size_t t = taker_of(key); t < kNames.size() && log) {
            REXLOG_WARN("Controls: {} and {} both use {}", name_of(t), name, BareKey(key));
          }
        }
        set.keys[i] = std::move(keys);
        player[i] = true;
      }
    }
  }
  std::array<std::string, size_t(PadControl::kCount)> why;
  for (size_t i = 0; i < kNames.size(); ++i) {
    if (player[i])
      continue;
    std::string kept;
    for (const std::string_view key : Tokens(defaults.keys[i])) {
      if (const size_t t = taker_of(key); t < kNames.size()) {
        if (why[i].empty()) {
          why[i] = std::string(key) + " went to " + name_of(t);
        }
      } else {
        kept += kept.empty() ? "" : ",";
        kept += key;
      }
    }
    if (!kept.empty() && kept != defaults.keys[i] && log) {
      REXLOG_INFO("Controls: {} {} -> {} ({})", name_of(i), defaults.keys[i], kept, why[i]);
    }
    set.keys[i] = std::move(kept);
  }
  for (size_t i = 0; i < kNames.size(); ++i) {
    if (player[i] || !set.keys[i].empty())
      continue;
    for (const std::string_view lost : Tokens(defaults.keys[i])) {
      const size_t t = taker_of(lost);
      for (const std::string_view key : Tokens(defaults.keys[t])) {
        if (!BoundInSet(set, key)) {
          set.keys[i] = key;
          if (log) {
            REXLOG_INFO("Controls: {} {} -> {} ({} went to {})", name_of(i), defaults.keys[i], key,
                        lost, name_of(t));
          }
          break;
        }
      }
      if (!set.keys[i].empty())
        break;
    }
  }
  for (size_t i = 0; i < kNames.size(); ++i) {
    if (player[i] || !set.keys[i].empty())
      continue;
    for (const std::string_view key : kSpareKeys) {
      if (!BoundInSet(set, key)) {
        set.keys[i] = key;
        if (log) {
          REXLOG_INFO("Controls: {} {} -> {} ({}, whose old keys are taken)", name_of(i),
                      defaults.keys[i], key, why[i]);
        }
        break;
      }
    }
    if (set.keys[i].empty()) {
      set.keys[i] = defaults.keys[i];
      if (log) {
        REXLOG_WARN("Controls: {} keeps {} ({}): every spare key is taken", name_of(i),
                    defaults.keys[i], why[i]);
      }
    }
  }
}

void LoadRate(const ControlLookup& lookup, std::string_view name, double& rate) {
  const auto value = lookup(name);
  if (value) {
    if (const auto parsed = ParseRampSeconds(*value)) {
      rate = *parsed;
    }
  }
}

}  // namespace

std::optional<double> ParseRampSeconds(std::string_view value) {
  double parsed = 0.0;
#if defined(__APPLE__)
  // libc++'s floating-point from_chars requires macOS 26 with current SDKs.
  // Use the C numeric locale without changing the process-wide locale, keeping
  // from_chars' decimal syntax, full-consumption and range checks on older Macs.
  if (value.empty() || value.front() == '+' ||
      std::string_view(" \t\r\n\f\v").find(value.front()) != std::string_view::npos) return std::nullopt;
  std::string_view digits = value;
  if (digits.starts_with('-')) digits.remove_prefix(1);
  if (digits.starts_with("0x") || digits.starts_with("0X")) return std::nullopt;
  locale_t numeric_locale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
  if (!numeric_locale) return std::nullopt;
  const std::string terminated(value);
  char* end = nullptr;
  errno = 0;
  parsed = strtod_l(terminated.c_str(), &end, numeric_locale);
  const int parse_error = errno;
  freelocale(numeric_locale);
  const bool valid = parse_error != ERANGE && end != terminated.data() &&
                     end == terminated.data() + terminated.size();
#else
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
  const bool valid = error == std::errc() && end == value.data() + value.size();
#endif
  if (valid && parsed >= 0.0) {
    return parsed;
  }
  return std::nullopt;
}

std::optional<bool> ParseMouseLookEnabled(std::string_view value) {
  if (value == "true") {
    return true;
  }
  if (value == "false") {
    return false;
  }
  return std::nullopt;
}

std::optional<int> ParseMouseLookSensitivity(std::string_view value) {
  const auto parsed = ParseRampSeconds(value);
  if (!parsed || !std::isfinite(*parsed) || *parsed <= 0.0) {
    return std::nullopt;
  }
  return int(std::lround(std::clamp(*parsed, double(kMouseLookSensitivityMin),
                                    double(kMouseLookSensitivityMax))));
}

// Keys after Forza Horizon 6 on FM4's default controller layout (gamedb.slt,
// ControllerLayouts, LayoutId 0): A handbrake, B gear up, X gear down, Y rewind,
// LB clutch, RB look straight back, LT brake, RT throttle, left stick steering,
// right stick look (down = look back), right stick press heat and damage,
// D-pad up and down telemetry; BACK changes the camera, START pauses. Controls
// the layout leaves unused still get a key, so nothing is out of reach.
Controls DefaultControls() {
  Controls controls;
  BindingSet& menu = controls.menu;
  menu.keys[size_t(PadControl::kA)] = "Return,Space";
  menu.keys[size_t(PadControl::kB)] = "Escape,Backspace";
  menu.keys[size_t(PadControl::kX)] = "X";
  menu.keys[size_t(PadControl::kY)] = "Y";
  menu.keys[size_t(PadControl::kLeftShoulder)] = "Q";
  menu.keys[size_t(PadControl::kRightShoulder)] = "E";
  menu.keys[size_t(PadControl::kLeftTrigger)] = "Z";
  menu.keys[size_t(PadControl::kRightTrigger)] = "C";
  // FM4's sliders ignore a short push of the stick and, while it is pushed, the
  // D-pad too, so WASD move like the arrows and the stick has keys of its own.
  menu.keys[size_t(PadControl::kLStickUp)] = "Numpad8";
  menu.keys[size_t(PadControl::kLStickDown)] = "Numpad2";
  menu.keys[size_t(PadControl::kLStickLeft)] = "Numpad4";
  menu.keys[size_t(PadControl::kLStickRight)] = "Numpad6";
  menu.keys[size_t(PadControl::kLStickPress)] = "F";
  menu.keys[size_t(PadControl::kRStickUp)] = "I";
  menu.keys[size_t(PadControl::kRStickDown)] = "K";
  menu.keys[size_t(PadControl::kRStickLeft)] = "J";
  menu.keys[size_t(PadControl::kRStickRight)] = "L";
  menu.keys[size_t(PadControl::kRStickPress)] = "G";
  menu.keys[size_t(PadControl::kDpadUp)] = "Up,W";
  menu.keys[size_t(PadControl::kDpadDown)] = "Down,S";
  menu.keys[size_t(PadControl::kDpadLeft)] = "Left,A";
  menu.keys[size_t(PadControl::kDpadRight)] = "Right,D";
  menu.keys[size_t(PadControl::kBack)] = "Tab";
  menu.keys[size_t(PadControl::kStart)] = "P";

  BindingSet& driving = controls.driving;
  driving.keys[size_t(PadControl::kA)] = "Space";
  driving.keys[size_t(PadControl::kB)] = "E";
  driving.keys[size_t(PadControl::kX)] = "Q";
  driving.keys[size_t(PadControl::kY)] = "R";
  driving.keys[size_t(PadControl::kLeftShoulder)] = "Shift";
  driving.keys[size_t(PadControl::kRightShoulder)] = "C";
  driving.keys[size_t(PadControl::kLeftTrigger)] = "S";
  driving.keys[size_t(PadControl::kRightTrigger)] = "W";
  driving.keys[size_t(PadControl::kLStickUp)] = "Numpad8";
  driving.keys[size_t(PadControl::kLStickDown)] = "Numpad2";
  driving.keys[size_t(PadControl::kLStickLeft)] = "A";
  driving.keys[size_t(PadControl::kLStickRight)] = "D";
  driving.keys[size_t(PadControl::kLStickPress)] = "V";
  driving.keys[size_t(PadControl::kRStickUp)] = "Up";
  driving.keys[size_t(PadControl::kRStickDown)] = "Down";
  driving.keys[size_t(PadControl::kRStickLeft)] = "Left";
  driving.keys[size_t(PadControl::kRStickRight)] = "Right";
  driving.keys[size_t(PadControl::kRStickPress)] = "H";
  driving.keys[size_t(PadControl::kDpadUp)] = "T";
  driving.keys[size_t(PadControl::kDpadDown)] = "G";
  driving.keys[size_t(PadControl::kDpadLeft)] = "Comma";
  driving.keys[size_t(PadControl::kDpadRight)] = "Period";
  driving.keys[size_t(PadControl::kBack)] = "Tab";
  driving.keys[size_t(PadControl::kStart)] = "Escape";

  controls.ramp = {0.17, 0.10, 0.10, 0.08, 0.15};
  controls.mouse_look = {true, 25};
  return controls;
}

Controls LoadControls(const ControlLookup& lookup, bool log) {
  Controls controls = DefaultControls();
  LoadSet(lookup, log, "menu_", controls.menu);
  LoadSet(lookup, log, "driving_", controls.driving);
  RampRates& ramp = controls.ramp;
  LoadRate(lookup, "ramp_steer_seconds", ramp.steer_seconds);
  LoadRate(lookup, "ramp_steer_return_seconds", ramp.steer_return_seconds);
  LoadRate(lookup, "ramp_pedal_seconds", ramp.pedal_seconds);
  LoadRate(lookup, "ramp_pedal_return_seconds", ramp.pedal_return_seconds);
  LoadRate(lookup, "ramp_look_seconds", ramp.look_seconds);
  if (const auto value = lookup("mouse_look")) {
    if (const auto enabled = ParseMouseLookEnabled(*value)) {
      controls.mouse_look.enabled = *enabled;
    }
  }
  if (const auto value = lookup("mouse_look_sensitivity")) {
    if (const auto sensitivity = ParseMouseLookSensitivity(*value)) {
      controls.mouse_look.sensitivity = *sensitivity;
    }
  }
  return controls;
}

Controls LoadControls(const config::HostConfig& config) {
  return LoadControls([&config](std::string_view name) { return config.Get(name); }, true);
}

std::string_view ControlFileName(PadControl control) { return kNames[size_t(control)].file; }

std::string UsableKeys(std::string_view bind) { return KnownKeys({}, bind, false); }

std::string BindKeyName(rex::ui::VirtualKey key) {
  using rex::ui::VirtualKey;
  // NumpadEnter shares the key code: Return is the name the defaults use.
  if (key == VirtualKey::kReturn) {
    return "Return";
  }
  if ((key >= VirtualKey::kF1 && key <= VirtualKey::kF24) || key == VirtualKey::kLButton ||
      key == VirtualKey::kRButton || key == VirtualKey::kMButton) {
    return {};
  }
  return rex::ui::VirtualKeyToString(key);
}

void ApplyBindings(const BindingSet& set) {
  auto binds = std::make_shared<rex::input::mnk::KeybindMap>();
  for (size_t i = 0; i < kNames.size(); ++i) {
    binds->emplace(kNames[i].option, set.keys[i]);
  }
  rex::input::mnk::SetKeybindOverrides(std::move(binds));
}

void ApplyMouseLook(const MouseLook& look) {
  rex::input::mnk::SetHoldLook({.enabled = look.enabled,
                                .degrees_per_count = MouseLookDegreesPerCount(look.sensitivity),
                                .hide_cursor = true});
}

}  // namespace fm4_static_recomp::input
