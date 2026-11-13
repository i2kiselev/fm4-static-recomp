// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "config/host_config.h"
#include "ui/hostui/menu.h"

namespace fm4_static_recomp::ui {

// The KEYBOARD tab beside GRAPHICS: the keyboard's keys of controls.toml (input/controls.h)
// in a DRIVING section, with the ramp times, and a MENUS section; a primary and an optional
// secondary key per pad control. Changes are drafted until SAVE & APPLY (Y), which writes
// only the edited lines, as the launcher's CONTROLS panel does (a value equal to the default
// removes its line), and applies them at once. The rows show the keys the game ends up using
// (LoadControls' rules for a key taken by another action).
struct ControlsPageServices {
  // The controller layout the game applied to the first player's pad.
  std::function<uint32_t()> controller_layout;
  // Takes the saved file in: called after a save; false when it could not.
  std::function<bool()> apply;
  std::function<std::chrono::steady_clock::time_point()> now = std::chrono::steady_clock::now;
};

// A key capture that sees no key press for this long ends by itself.
inline constexpr auto kCaptureTimeout = std::chrono::seconds(5);

std::unique_ptr<hostui::MenuScreen> CreateControlsPage(config::HostConfig& config,
                                                       ControlsPageServices services);

// The MOUSE tab after KEYBOARD: mouse look in races and its sensitivity, in the same
// controls.toml and saved the same way (controller_layout is not used).
std::unique_ptr<hostui::MenuScreen> CreateMousePage(config::HostConfig& config,
                                                    ControlsPageServices services);

}  // namespace fm4_static_recomp::ui
