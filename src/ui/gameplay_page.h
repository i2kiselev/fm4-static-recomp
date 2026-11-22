// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config/host_config.h"
#include "ui/hostui/menu.h"

namespace fm4_static_recomp::ui {

// The tabs after MOUSE, kept in gameplay.toml, as ui/settings_layout.h lays them out: GAME, and MODS once its
// switch at the bottom of GAME is on (the switch shows once MODS has a row to show). SAVE & APPLY (Y) writes a
// page's settings and sets them at once.
struct SettingsPageInfo {
  std::string title;
  bool mod = false;
};
std::vector<SettingsPageInfo> SettingsPages();
std::unique_ptr<hostui::MenuScreen> CreateSettingsPage(bool mod, config::HostConfig& config);
// Called after SAVE & APPLY changes which tabs there are.
void SetSettingsTabsChanged(std::function<void()> changed);

// Every setting on one page, whatever the layout (the unit tests).
std::unique_ptr<hostui::MenuScreen> CreateGameplayPage(config::HostConfig& config);

// Sets the saved values at startup, below a launch option for the same setting.
void ApplySavedGameplaySettings(const config::HostConfig& config);

}  // namespace fm4_static_recomp::ui
