// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "config/host_config.h"

namespace fm4_static_recomp {

// wheel.toml beside controls.toml: the SDK's wheel_* settings, written by the launcher's WHEEL
// screen and the in-game WHEEL tab, plus two keys of our own: wheel_type (a WheelType id or
// "custom") and wheel_rotation (degrees, as set in the wheel's own software; default 900).

// One row of the force feedback settings. Basic rows sit above the wheel type's Advanced ones.
struct WheelRow {
  std::string_view key, label, description;
  int min, max, step, fallback;
  bool basic;
};

// A wheel type sets the Advanced rows that shape the force, all but STEERING LOCK.
struct WheelType {
  std::string_view id, label;
  std::array<std::pair<std::string_view, int>, 7> values;
};

inline constexpr std::string_view kSteeringLockKey = "steering_lock";
inline constexpr int kDefaultWheelRotation = 900;

std::span<const WheelRow> WheelRows();
std::span<const WheelType> WheelTypes();
const WheelRow* FindWheelRow(std::string_view key);

// The saved wheel type; an unknown or missing one is "custom" when the file has any of the
// types' values, otherwise "direct".
std::string WheelTypeOf(const config::HostConfig& config);
// Writes the type's values and wheel_type.
void SetWheelType(config::HostConfig& config, std::string_view id);
// The row's value as the game runs it: the file's, else the wheel type's, else the fallback.
int WheelValue(const config::HostConfig& config, std::string_view key);
// Clamps and writes; a wheel type's value makes the type "custom".
void SetWheelValue(config::HostConfig& config, std::string_view key, int value);

int WheelRotation(const config::HostConfig& config);
// The smallest lock wheel_steering_scale reaches, to the next 10 degrees.
int MinSteeringLock(int rotation);
// wheel_steering_scale shown as the degrees of wheel turn for full lock, to the nearest 10.
int SteeringLockDegrees(int rotation, int scale);
int SteeringLockDegrees(const config::HostConfig& config);
void SetSteeringLock(config::HostConfig& config, int degrees);

// At startup, below a launch option for the same setting: every wheel_* setting in the file,
// then, with a wheel on, the wheel type's values the file leaves out.
void ApplySavedWheelSettings(const config::HostConfig& config);

// The wheel update hook's decision for one controller slot: hold the forces outside driving,
// or nullopt to leave them (no wheel on, or a slot without a wheel).
std::optional<bool> WheelForcesHeld(bool wheel_enabled, uint8_t wheel_detected, uint8_t driving);

// For the launcher: the rows, the wheel types and every other wheel_* setting.
bool WriteWheelSchema(const std::filesystem::path& path);

}  // namespace fm4_static_recomp
