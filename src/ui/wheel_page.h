// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <memory>

#include "config/host_config.h"
#include "ui/hostui/menu.h"

namespace fm4_static_recomp::ui {

// The WHEEL tab after GAMEPLAY, while a wheel is on: the wheel type and the force feedback rows
// of wheel.toml. SAVE & APPLY (Y) writes them and sets them at once; leaving a custom set for a
// wheel type asks for a second press.
std::unique_ptr<hostui::MenuScreen> CreateWheelPage(config::HostConfig& config);

}  // namespace fm4_static_recomp::ui
