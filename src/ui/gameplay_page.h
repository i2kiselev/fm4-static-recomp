// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "config/host_config.h"
#include "ui/hostui/menu.h"

namespace fm4_static_recomp::ui {

// The GAMEPLAY tab after MOUSE, kept in gameplay.toml: the experimental Quick Race lap count
// (fm4_quick_race_laps; DEFAULT removes the line, the next track select and race take it) and
// CUSTOM DECALS (fm4_custom_decals_folder, fm4_all_sponsor_decals; the paint editor takes them the
// next time it opens). SAVE & APPLY (Y) writes them and sets them at once.
std::unique_ptr<hostui::MenuScreen> CreateGameplayPage(config::HostConfig& config);

// Opens a folder chooser starting at `current` and calls `done` with the folder (nullopt when
// cancelled), from any thread. The app sets it (a native dialog); without one the row does nothing.
using FolderPicker = std::function<void(const std::string& current,
                                        std::function<void(std::optional<std::string>)> done)>;
void SetFolderPicker(FolderPicker picker);

// Sets the saved values at startup, below a launch option for the same setting.
void ApplySavedGameplaySettings(const config::HostConfig& config);

}  // namespace fm4_static_recomp::ui
