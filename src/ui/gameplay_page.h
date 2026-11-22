// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <memory>

#include "config/host_config.h"
#include "ui/hostui/menu.h"

namespace fm4_static_recomp::ui {

// The GAMEPLAY tab after MOUSE, kept in gameplay.toml: the experimental Quick Race lap count
// (fm4_quick_race_laps; DEFAULT removes the line, the next track select and race take it) and
// the DIFFICULTY options (fm4_race_payouts, fm4_repair_costs, fm4_resale_value;
// from the next start). SAVE & APPLY (Y) writes them and
// sets them at once.
std::unique_ptr<hostui::MenuScreen> CreateGameplayPage(config::HostConfig& config);

// Sets the saved values at startup, below a launch option for the same setting.
void ApplySavedGameplaySettings(const config::HostConfig& config);

}  // namespace fm4_static_recomp::ui
