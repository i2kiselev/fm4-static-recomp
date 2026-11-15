// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
#include "wheel/wheel_mapping.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace fm4_static_recomp::wheel {

namespace sdl = rex::input::sdl;
namespace ffb = rex::input::xinputd_ffb;

const Device* FindDevice(const std::vector<Device>& devices, std::string_view match) {
  if (match.empty()) {
    return nullptr;
  }
  for (const Device& d : devices) {
    if (sdl::WheelDeviceMatches(match, d.name, d.vendor, d.product, d.path)) {
      return &d;
    }
  }
  return nullptr;
}

std::string DeviceMatch(const Device& device, const std::vector<Device>& all) {
  const auto count = [&](std::string_view match) {
    return std::count_if(all.begin(), all.end(), [&](const Device& d) {
      return sdl::WheelDeviceMatches(match, d.name, d.vendor, d.product, d.path);
    });
  };
  if (device.vendor || device.product) {
    char ids[16];
    std::snprintf(ids, sizeof(ids), "%04x:%04x", device.vendor, device.product);
    if (count(ids) == 1) {
      return ids;
    }
  }
  if (!device.name.empty() && count(device.name) == 1) {
    return device.name;
  }
  return "path:" + device.path;
}

int SteeringScale(const Settings& s) {
  int64_t scale = std::clamp(s.steering_scale, 10, 1000);
  if (s.game_rotation > 0 && s.wheel_rotation > 0) {
    scale = scale * s.wheel_rotation / s.game_rotation;
  }
  return int(std::clamp<int64_t>(scale, 10, 1000));
}

uint16_t ButtonsFromNames(std::string_view names) {
  uint16_t bits = 0;
  size_t start = 0;
  while (start <= names.size()) {
    size_t end = names.find_first_of("+,", start);
    if (end == std::string_view::npos) {
      end = names.size();
    }
    const std::string_view name = names.substr(start, end - start);
    if (!name.empty()) {
      // The SDK's binding parser knows the pad names; any input will do.
      for (const auto& b : sdl::ParseWheelButtonBindings(std::string(name) + "=0")) {
        bits |= b.xbuttons;
      }
    }
    start = end + 1;
  }
  return bits;
}

Composer::Composer(Settings settings) : s_(std::move(settings)) {
  pedals_[0] = sdl::ParseWheelAxisBinding(s_.throttle);
  pedals_[1] = sdl::ParseWheelAxisBinding(s_.brake);
  pedals_[2] = sdl::ParseWheelAxisBinding(s_.clutch);
  buttons_ = sdl::ParseWheelButtonBindings(s_.buttons);
  clutch_buttons_ = ButtonsFromNames(s_.clutch_button);
  shifter_ = std::any_of(buttons_.begin(), buttons_.end(),
                         [](const sdl::WheelButtonBinding& b) { return b.gear != 0; });
}

const Device* Composer::Base(const Snapshot& snap) const {
  if (!s_.device.empty()) {
    return FindDevice(snap.devices, s_.device);
  }
  for (const Device& d : snap.devices) {
    if (d.wheel) {
      return &d;
    }
  }
  // SDL types most Logitech, Thrustmaster and Fanatec bases; the SDK knows a few more vendors.
  for (const Device& d : snap.devices) {
    if (sdl::IsKnownWheelVendor(d.vendor)) {
      return &d;
    }
  }
  return nullptr;
}

const Device* Composer::On(const Snapshot& snap, const std::string& device) const {
  return device.empty() ? Base(snap) : FindDevice(snap.devices, device);
}

uint8_t Composer::Pedal(const Snapshot& snap, int which) const {
  const auto& binding = pedals_[which];
  if (!binding) {
    return 0;
  }
  const Device* d = On(snap, binding->device);
  if (!d || binding->axis < 0 || binding->axis >= int(d->axes.size())) {
    return 0;
  }
  const int bottom =
      s_.deadzone_bottom[which] < 0 ? s_.pedal_deadzone : s_.deadzone_bottom[which];
  return sdl::PedalFromAxis(d->axes[binding->axis],
                            sdl::ResolveAxisDirection(binding->direction, d->rest[binding->axis]),
                            bottom, s_.deadzone_top[which]);
}

rex::input::X_INPUT_GAMEPAD Composer::Compose(const Snapshot& snap) const {
  rex::input::X_INPUT_GAMEPAD pad{};
  if (const Device* base = Base(snap);
      base && s_.steering_axis >= 0 && s_.steering_axis < int(base->axes.size())) {
    pad.thumb_lx = sdl::SteeringFromAxis(base->axes[s_.steering_axis], s_.steering_invert,
                                         SteeringScale(s_), s_.steering_deadzone);
  }
  uint8_t right = Pedal(snap, 0), left = Pedal(snap, 1);
  uint16_t pressed = Pedal(snap, 2) >= 128 ? clutch_buttons_ : 0;
  int16_t gear = 0;
  for (const auto& b : buttons_) {
    const Device* d = On(snap, b.device);
    if (!d) {
      continue;
    }
    bool on = false;
    if (b.axis >= 0 && b.axis < int(d->axes.size())) {
      on = sdl::PedalFromAxis(d->axes[b.axis],
                              sdl::ResolveAxisDirection(b.axis_direction, d->rest[b.axis]), 0,
                              0) >= 128;
    } else if (b.button >= 0 && b.button < int(d->buttons.size())) {
      on = d->buttons[b.button];
    } else if (b.hat >= 0 && b.hat < int(d->hats.size())) {
      on = (d->hats[b.hat] & b.hat_mask) != 0;
    }
    if (!on) {
      continue;
    }
    if (b.gear) {
      gear = b.gear;
    }
    pressed |= b.xbuttons;
    if (b.left_trigger) {
      left = 255;
    }
    if (b.right_trigger) {
      right = 255;
    }
  }
  pad.buttons = pressed;
  pad.left_trigger = left;
  pad.right_trigger = right;
  // With gears bound, the held gear on the right stick's X axis (-1 reverse, 0 neutral), as
  // Forza Horizon reads an H-shifter; FM4 shares its wheel code, unconfirmed there.
  if (shifter_) {
    pad.thumb_rx = gear;
  }
  return pad;
}

FfbMode ParseFfbMode(std::string_view text) {
  if (text == "off") {
    return FfbMode::kOff;
  }
  if (text == "rumble") {
    return FfbMode::kRumble;
  }
  return FfbMode::kEffects;
}

namespace {

sdl::WheelEffectScale Scale(const Settings& s, int condition_percent) {
  sdl::WheelEffectScale scale;
  scale.strength_percent = 100;  // applied here, up to 200 %
  scale.magnitude_max = kEffectMagnitudeMax;
  scale.saturation_max = kEffectSaturationMax;
  scale.invert = s.ffb_invert;
  scale.min_force_percent = s.ffb_min_force;
  scale.condition_percent = std::clamp(condition_percent, 0, 200);
  return scale;
}

int16_t Strength(int32_t value, int percent) {
  const int64_t scaled = int64_t(value) * std::clamp(percent, 0, 200) / 100;
  return int16_t(std::clamp<int64_t>(scaled, -32767, 32767));
}

}  // namespace

int16_t ConstantLevel(const ffb::Effect& effect, const Settings& s) {
  return Strength(sdl::EffectConstantLevel(effect, Scale(s, 100)), s.ffb_strength);
}

sdl::WheelCondition ConditionValues(const ffb::Effect& effect, const Settings& s) {
  int share = 100;
  switch (effect.type) {
    case ffb::kSpring:
      share = s.ffb_spring;
      break;
    case ffb::kDamper:
      share = s.ffb_damper;
      break;
    case ffb::kFriction:
      share = s.ffb_friction;
      break;
    case ffb::kInertia:
      share = s.ffb_inertia;
      break;
    default:
      break;
  }
  sdl::WheelCondition c = sdl::EffectCondition(effect, Scale(s, share));
  c.right_coeff = Strength(c.right_coeff, s.ffb_strength);
  c.left_coeff = Strength(c.left_coeff, s.ffb_strength);
  return c;
}

int16_t TestForceLevel(const Settings& s) {
  const int16_t level = Strength(16384, s.ffb_strength);
  return s.ffb_invert ? int16_t(-level) : level;
}

int16_t RumbleMagnitude(uint16_t left, uint16_t right, const Settings& s) {
  const int64_t road = int64_t(left) * std::clamp(s.ffb_rumble_road, 0, 100) / 100;
  const int64_t engine = int64_t(right) * std::clamp(s.ffb_rumble_engine, 0, 100) / 100;
  const int64_t level = std::max(road, engine) * 32767 / 65535 *
                        std::clamp(s.ffb_rumble, 0, 100) / 100;
  return int16_t(std::clamp<int64_t>(level, 0, 32767));
}

}  // namespace fm4_static_recomp::wheel
