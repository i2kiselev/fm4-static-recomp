// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <memory>

#include "config/host_config.h"
#include "ui/hostui/menu.h"

namespace fm4_static_recomp::ui {

// The GAMEPLAY tab after MOUSE: the experimental Quick Race lap count, kept in gameplay.toml
// as fm4_quick_race_laps. SAVE & APPLY (Y) writes it (DEFAULT removes the line) and sets it
// at once; the next track select and race take it.
std::unique_ptr<hostui::MenuScreen> CreateGameplayPage(config::HostConfig& config);

// Sets the saved values at startup, below a launch option for the same setting.
void ApplySavedGameplaySettings(const config::HostConfig& config);

}  // namespace fm4_static_recomp::ui
