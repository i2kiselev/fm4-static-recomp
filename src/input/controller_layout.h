// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <string_view>

#include "input/controls.h"

namespace fm4_static_recomp::input {

// FM4's controller layouts (gamedb.slt, ControllerLayouts, LayoutId 0 to 12), chosen per
// profile in the game's controller options: which pad button does what while driving.
inline constexpr uint32_t kControllerLayoutCount = 13;

// The game applies the first player's pad layout (Fm4ControllerLayoutApplied in
// game_hooks.cpp); 0, the default, until it has. Any thread.
void SetControllerLayout(uint32_t layout);
uint32_t ControllerLayout();

// The first player's profile steers with the D-pad (D-PAD STEERING); false until a layout
// build says so. Any thread.
void SetDpadSteering(bool on);
bool DpadSteering();

// What the control does while driving under `layout`, e.g. "Gear up"; empty for a control
// the layout leaves unused. An unknown layout reads as the default one.
std::string_view DrivingAction(uint32_t layout, PadControl control);

}  // namespace fm4_static_recomp::input
