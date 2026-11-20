// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "wheel_settings.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/input/xinputd_ffb.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

#include "built_in_defaults.h"

REXCVAR_DECLARE(bool, wheel_enabled);

namespace fm4_static_recomp {
namespace {

constexpr std::string_view kTypeKey = "wheel_type";
constexpr std::string_view kRotationKey = "wheel_rotation";
constexpr std::string_view kScaleKey = "wheel_steering_scale";
constexpr std::string_view kCustom = "custom";
constexpr int kMinSteeringLock = 180;
// wheel_steering_scale's range in the SDK.
constexpr int kMinScale = 10;
constexpr int kMaxScale = 1000;

// The road and engine rows are the SDK's shares of wheel_ffb_rumble, capped at 100.
constexpr WheelRow kRows[] = {
    {"wheel_ffb_strength", "STRENGTH", "Turn it down if the wheel is too strong to hold.", 0,
     100, 5, 85, true},
    {"wheel_ffb_constant", "STEERING WEIGHT",
     "Turn it up for a heavier wheel in corners, down for a lighter one.", 0, 200, 5, 100,
     false},
    {"wheel_ffb_spring", "SELF-CENTRING", "Turn it up to pull the wheel back to the middle harder.",
     0, 200, 10, 100, false},
    {"wheel_ffb_damper", "DAMPING", "Turn it up to make the wheel slower and heavier to turn.", 0,
     200, 10, 100, false},
    {"wheel_ffb_rumble_road", "ROAD EFFECTS", "Turn it up to feel kerbs, bumps and crashes more.",
     0, 100, 10, 100, false},
    {"wheel_ffb_rumble_engine", "ENGINE VIBRATION",
     "Turn it down if the wheel buzzes all the time.", 0, 100, 10, 100, false},
    {"wheel_ffb_centre", "CENTRE FEEL",
     "Turn it up if the wheel feels loose and empty when you drive straight.", 0, 100, 5, 0,
     false},
    {"wheel_ffb_jolt", "SMOOTH JOLTS",
     "Turn it up to soften sudden kicks, like gear changes; kerbs stay sharp.", 0, 100, 5, 0,
     false},
    {kSteeringLockKey, "STEERING LOCK",
     "Turn it down to steer quicker: less turning of your wheel for full lock.",
     kMinSteeringLock, kDefaultWheelRotation, 10, kDefaultWheelRotation, false},
};

// Starting values; the direct-drive ones are tuned on recorded R5 forces.
constexpr WheelType kTypes[] = {
    {"direct",
     "DIRECT DRIVE",
     {{{"wheel_ffb_constant", 100},
       {"wheel_ffb_spring", 100},
       {"wheel_ffb_damper", 100},
       {"wheel_ffb_rumble_road", 100},
       {"wheel_ffb_rumble_engine", 30},
       {"wheel_ffb_centre", 50},
       {"wheel_ffb_jolt", 50}}}},
    {"belt",
     "BELT",
     {{{"wheel_ffb_constant", 100},
       {"wheel_ffb_spring", 100},
       {"wheel_ffb_damper", 80},
       {"wheel_ffb_rumble_road", 100},
       {"wheel_ffb_rumble_engine", 50},
       {"wheel_ffb_centre", 30},
       {"wheel_ffb_jolt", 30}}}},
    {"gear",
     "GEAR",
     {{{"wheel_ffb_constant", 100},
       {"wheel_ffb_spring", 100},
       {"wheel_ffb_damper", 60},
       {"wheel_ffb_rumble_road", 100},
       {"wheel_ffb_rumble_engine", 60},
       {"wheel_ffb_centre", 20},
       {"wheel_ffb_jolt", 0}}}},
};

const WheelType* FindType(std::string_view id) {
  for (const WheelType& type : kTypes) {
    if (type.id == id) {
      return &type;
    }
  }
  return nullptr;
}

std::optional<int> TypeValue(const WheelType& type, std::string_view key) {
  for (const auto& [name, value] : type.values) {
    if (name == key) {
      return value;
    }
  }
  return std::nullopt;
}

bool IsTypeKey(std::string_view key) { return TypeValue(kTypes[0], key).has_value(); }

std::optional<int> ParseInt(const std::optional<std::string>& text) {
  if (!text) {
    return std::nullopt;
  }
  char* end = nullptr;
  const long value = std::strtol(text->c_str(), &end, 10);
  if (end == text->c_str() || *end) {
    return std::nullopt;
  }
  return int(std::clamp(value, -1000000L, 1000000L));
}

bool FromCommandLine(std::string_view name) {
  return rex::cvar::GetFlagSource(name) == rex::cvar::Source::kCommandLine;
}

std::string JsonString(std::string_view text) {
  std::string out = "\"";
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      out += fmt::format("\\u{:04x}", int(c));
    } else {
      out += c;
    }
  }
  return out + "\"";
}

}  // namespace

std::span<const WheelRow> WheelRows() { return kRows; }

std::span<const WheelType> WheelTypes() { return kTypes; }

const WheelRow* FindWheelRow(std::string_view key) {
  for (const WheelRow& row : kRows) {
    if (row.key == key) {
      return &row;
    }
  }
  return nullptr;
}

std::string WheelTypeOf(const config::HostConfig& config) {
  const auto saved = config.Get(kTypeKey);
  if (saved && (FindType(*saved) || *saved == kCustom)) {
    return *saved;
  }
  for (const auto& [key, value] : kTypes[0].values) {
    if (config.Get(key)) {
      return std::string(kCustom);
    }
  }
  return std::string(kTypes[0].id);
}

void SetWheelType(config::HostConfig& config, std::string_view id) {
  const WheelType* const type = FindType(id);
  if (!type) {
    return;
  }
  for (const auto& [key, value] : type->values) {
    config.Set(key, std::to_string(value));
  }
  config.Set(kTypeKey, config::Quote(type->id));
}

int WheelValue(const config::HostConfig& config, std::string_view key) {
  if (key == kSteeringLockKey) {
    return SteeringLockDegrees(config);
  }
  const WheelRow* const row = FindWheelRow(key);
  if (!row) {
    return 0;
  }
  if (const auto saved = ParseInt(config.Get(key))) {
    return std::clamp(*saved, row->min, row->max);
  }
  if (const WheelType* const type = FindType(WheelTypeOf(config))) {
    if (const auto value = TypeValue(*type, key)) {
      return *value;
    }
  }
  return row->fallback;
}

void SetWheelValue(config::HostConfig& config, std::string_view key, int value) {
  if (key == kSteeringLockKey) {
    SetSteeringLock(config, value);
    return;
  }
  const WheelRow* const row = FindWheelRow(key);
  if (!row) {
    return;
  }
  config.Set(key, std::to_string(std::clamp(value, row->min, row->max)));
  if (IsTypeKey(key)) {
    config.Set(kTypeKey, config::Quote(kCustom));
  }
}

int WheelRotation(const config::HostConfig& config) {
  const int rotation = ParseInt(config.Get(kRotationKey)).value_or(kDefaultWheelRotation);
  return std::max(rotation, kMinSteeringLock);
}

int MinSteeringLock(int rotation) {
  const int reachable = (rotation * 100 / kMaxScale + 9) / 10 * 10;
  return std::max(reachable, kMinSteeringLock);
}

int SteeringLockDegrees(int rotation, int scale) {
  const int degrees = int(std::lround(rotation * 10.0 / std::clamp(scale, kMinScale, kMaxScale))) * 10;
  return std::clamp(degrees, MinSteeringLock(rotation), rotation);
}

int SteeringLockDegrees(const config::HostConfig& config) {
  return SteeringLockDegrees(WheelRotation(config), ParseInt(config.Get(kScaleKey)).value_or(100));
}

void SetSteeringLock(config::HostConfig& config, int degrees) {
  const int rotation = WheelRotation(config);
  const int lock = std::clamp(degrees, MinSteeringLock(rotation), rotation);
  const long scale = std::lround(rotation * 100.0 / lock);
  config.Set(kScaleKey, std::to_string(std::clamp(int(scale), kMinScale, kMaxScale)));
}

void ApplySavedWheelSettings(const config::HostConfig& config) {
  std::vector<std::string> from_file;
  for (const std::string& name : rex::cvar::ListFlags()) {
    if (!name.starts_with("wheel_")) {
      continue;
    }
    auto value = config.Get(name);
    if (!value) {
      continue;
    }
    from_file.push_back(name);
    // A row runs what the WHEEL page shows: clamped, or the wheel type's for a garbled value.
    if (FindWheelRow(name)) {
      value = std::to_string(WheelValue(config, name));
    } else if (name == kScaleKey) {
      value = std::to_string(std::clamp(ParseInt(value).value_or(100), kMinScale, kMaxScale));
    }
    if (FromCommandLine(name)) {
      REXLOG_INFO("Wheel settings: {} comes from the command line, not {}", name,
                  config.path().string());
    } else if (!rex::cvar::SetFlagByName(name, *value)) {
      REXLOG_WARN("Wheel settings: ignored {} = {} in {}", name, *value, config.path().string());
    } else {
      REXLOG_INFO("Wheel settings: {} = {}", name, *value);
    }
  }
  if (!rex::cvar::Query<bool>("wheel_enabled")) {
    return;
  }
  const WheelType* const type = FindType(WheelTypeOf(config));
  if (!type) {
    return;
  }
  for (const auto& [key, value] : type->values) {
    if (std::ranges::find(from_file, key) != from_file.end() || FromCommandLine(key)) {
      continue;
    }
    rex::cvar::SetFlagByName(key, std::to_string(value));
  }
  REXLOG_INFO("Wheel settings: wheel type {}", type->id);
}

std::optional<bool> WheelForcesHeld(bool wheel_enabled, uint8_t wheel_detected, uint8_t driving) {
  if (!wheel_enabled || !wheel_detected) {
    return std::nullopt;
  }
  return driving == 0;
}

bool WriteWheelSchema(const std::filesystem::path& path) {
  std::string out = "{\"version\":1,\"rows\":[";
  for (const WheelRow& row : kRows) {
    out += std::string(&row == kRows ? "" : ",") + "{\"key\":" + JsonString(row.key) +
           ",\"label\":" + JsonString(row.label) + ",\"description\":" +
           JsonString(row.description) + ",\"min\":" + std::to_string(row.min) +
           ",\"max\":" + std::to_string(row.max) + ",\"step\":" + std::to_string(row.step) +
           ",\"fallback\":" + std::to_string(row.fallback) +
           ",\"basic\":" + (row.basic ? "true" : "false") + "}";
  }
  out += "],\"types\":[";
  for (const WheelType& type : kTypes) {
    out += std::string(&type == kTypes ? "" : ",") + "{\"id\":" + JsonString(type.id) +
           ",\"label\":" + JsonString(type.label) + ",\"values\":{";
    for (const auto& [key, value] : type.values) {
      out += std::string(key == type.values[0].first ? "" : ",") + JsonString(key) + ":" +
             std::to_string(value);
    }
    out += "}}";
  }
  out += "],\"expert\":[";
  bool first = true;
  for (const std::string& name : rex::cvar::ListFlags()) {
    const rex::cvar::FlagEntry* const info = rex::cvar::GetFlagInfo(name);
    // wheel_steering_scale is STEERING LOCK's.
    if (!name.starts_with("wheel_") || FindWheelRow(name) || name == kScaleKey || !info) {
      continue;
    }
    out += std::string(first ? "" : ",") + "{\"key\":" + JsonString(name) +
           ",\"description\":" + JsonString(info->description) +
           ",\"default\":" + JsonString(BuiltInDefault(name).value_or(info->default_value)) +
           "}";
    first = false;
  }
  out += "]}\n";
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << out;
  return bool(file);
}

}  // namespace fm4_static_recomp

// Wheel update (1.0 823A42A0), for every polled controller slot: r3 is the slot's wheel force
// state, byte 4 set when the slot is a wheel, byte 5 while driving.
void Fm4WheelUpdate(PPCRegister& r3) {
  if (!REXCVAR_GET(wheel_enabled)) {
    return;
  }
  const uint8_t* const state = rex::system::kernel_state()->memory()->virtual_membase() + r3.u32;
  const auto held = fm4_static_recomp::WheelForcesHeld(true, state[4], state[5]);
  static std::atomic<int> was_held{-1};
  if (held && was_held.exchange(*held, std::memory_order_relaxed) != int(*held)) {
    rex::input::xinputd_ffb::SetForcesHeld(*held);
    REXLOG_INFO("Wheel: {}", *held ? "not driving, forces held" : "driving, forces on");
  }
}
