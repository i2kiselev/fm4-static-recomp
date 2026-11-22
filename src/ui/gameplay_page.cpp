// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "ui/gameplay_page.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "quick_race_laps.h"
#include "ui/settings_layout.h"

REXCVAR_DEFINE_BOOL(fm4_mod_menu, false, "Forza Motorsport 4",
                    "SHOW MODS TAB (PC OPTIONS > GAME): the MODS tab with the settings that change the game "
                    "beyond its own options");

namespace fm4_static_recomp::ui {
namespace {

using hostui::MenuRow;
using hostui::MenuScreen;

std::mutex g_tabs_changed_mutex;
std::function<void()> g_tabs_changed;

constexpr const char* kLaps = "fm4_quick_race_laps";
constexpr const char* kModMenu = "fm4_mod_menu";

// A saved count the game takes, or nullopt.
std::optional<int> ParseLaps(const std::string& value) {
  const int laps = std::atoi(value.c_str());
  if (std::to_string(laps) != value || laps < 0 || laps > kQuickRaceLapsMax) {
    return std::nullopt;
  }
  return laps;
}

bool FromCommandLine(const char* name) {
  return rex::cvar::GetFlagSource(name) == rex::cvar::Source::kCommandLine;
}

std::string LaunchOptionBadge(const char* name) {
  return FromCommandLine(name) ? std::string("Set by a launch option for this session.") : std::string();
}

std::string CurrentValue(const char* name) { return rex::cvar::GetFlagByName(name); }

// A page's settings: each drafted value (as fm4_* takes it) until SAVE & APPLY.
struct Draft {
  explicit Draft(config::HostConfig& config) : config(config) {}

  config::HostConfig& config;
  std::map<std::string, std::string> pending;  // cvar -> drafted value
  bool leave_warned = false;
  bool applied = false;
  bool save_failed = false;

  std::string Value(const char* name) const {
    const auto found = pending.find(name);
    return found != pending.end() ? found->second : CurrentValue(name);
  }

  void Change(const char* name, std::string value) {
    if (value == Value(name)) return;  // a press at a row's end: the page's state stands
    if (value == CurrentValue(name)) {
      pending.erase(name);
    } else {
      pending[name] = std::move(value);
    }
    leave_warned = false;
    applied = false;
    save_failed = false;
  }

  int Laps() const { return std::atoi(Value(kLaps).c_str()); }

  void SaveAndApply() {
    if (pending.empty()) return;
    for (const auto& [name, value] : pending) {
      // A setting at its default is no line in the file.
      if (value == (name == kLaps ? "0" : "false")) {
        config.Remove(name);
      } else {
        config.Set(name, value);
      }
    }
    if (!config.Save()) {
      REXLOG_ERROR("Gameplay page: could not write {}", config.path().string());
      save_failed = true;
      return;
    }
    bool tabs_changed = false;
    for (const auto& [name, value] : pending) {
      rex::cvar::SetFlagByName(name, value);
      REXLOG_INFO("Gameplay page: {} = {}", name, value);
      tabs_changed = tabs_changed || name == kModMenu;
    }
    pending.clear();
    leave_warned = false;
    applied = true;
    if (tabs_changed) {
      std::function<void()> changed;
      {
        std::lock_guard lock(g_tabs_changed_mutex);
        changed = g_tabs_changed;
      }
      if (changed) changed();
    }
  }
};

std::string LapsLabel(int laps) { return laps ? std::to_string(laps) : std::string("DEFAULT"); }

MenuRow LapsRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "QUICK RACE LAPS";
  row.description =
      "Experimental: the number of laps of every Quick Race, on every track, up to 99. "
      "Default keeps each track's own count. Track select shows the number from the next "
      "time it opens. Long races are not tested yet: tyre wear, the AI and the results "
      "screen may not hold up.";
  row.value = [draft] { return LapsLabel(draft->Laps()); };
  row.adjust = [draft](int direction) {
    draft->Change(kLaps, std::to_string(std::clamp(draft->Laps() + direction, 0, kQuickRaceLapsMax)));
  };
  row.badge = [] { return LaunchOptionBadge(kLaps); };
  row.unapplied = [draft] { return draft->pending.contains(kLaps); };
  return row;
}

MenuRow ModMenuRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "SHOW MODS TAB";
  row.description =
      "Shows the MODS tab next to this one, with the settings that change the game beyond its own "
      "options. Turning it off only hides the tab: what you set there stays as it is.";
  row.value = [draft] { return std::string(draft->Value(kModMenu) == "true" ? "ON" : "OFF"); };
  row.adjust = [draft](int) { draft->Change(kModMenu, draft->Value(kModMenu) == "true" ? "false" : "true"); };
  row.badge = [] { return LaunchOptionBadge(kModMenu); };
  row.unapplied = [draft] { return draft->pending.contains(kModMenu); };
  return row;
}

MenuRow Heading(const char* label) {
  MenuRow row;
  row.label = label;
  row.heading = true;
  row.divider_above = true;
  return row;
}

// The rows this build has: each feature adds its own entry here, and the layout table (settings_layout.h)
// already says where it goes. A row the layout names but no entry builds is left out, and so is a heading or a
// tab left with no rows.
using RowMaker = MenuRow (*)(const std::shared_ptr<Draft>&);
struct RowEntry {
  SettingRow setting;
  RowMaker make;
};
constexpr RowEntry kRows[] = {
    {SettingRow::kQuickRaceLaps, LapsRow},
};

RowMaker MakerOf(SettingRow setting) {
  for (const RowEntry& entry : kRows) {
    if (entry.setting == setting) return entry.make;
  }
  return nullptr;
}

bool HasRows(const SettingsTab& tab) {
  for (const SettingsSection& section : tab.sections) {
    for (const SettingRow setting : section.rows) {
      if (MakerOf(setting)) return true;
    }
  }
  return false;
}

// The MODS switch shows only when the MODS tab would have something on it.
bool ModsTabUsable() { return HasRows(ActiveSettingsLayout().mod); }

std::unique_ptr<MenuScreen> CreatePage(const SettingsTab& tab, config::HostConfig& config) {
  auto draft = std::make_shared<Draft>(config);
  std::vector<MenuRow> rows;
  for (const SettingsSection& section : tab.sections) {
    std::vector<MenuRow> built;
    for (const SettingRow setting : section.rows) {
      if (setting == SettingRow::kModMenu) {
        if (ModsTabUsable()) built.push_back(ModMenuRow(draft));
      } else if (const RowMaker make = MakerOf(setting)) {
        built.push_back(make(draft));
      }
    }
    if (built.empty()) continue;
    if (section.heading) rows.push_back(Heading(section.heading));
    for (MenuRow& row : built) rows.push_back(std::move(row));
  }
  // The first heading sits at the top: no line above it.
  if (!rows.empty() && rows.front().heading) rows.front().divider_above = false;
  auto screen = std::make_unique<MenuScreen>(tab.title, std::move(rows));
  screen->set_apply([draft] { draft->SaveAndApply(); });
  screen->set_leave([draft] {
    if (draft->pending.empty() || draft->leave_warned) return true;
    draft->leave_warned = true;
    return false;
  });
  screen->set_status([draft] {
    if (draft->save_failed) {
      return hostui::MenuStatus{.text = "COULD NOT SAVE GAMEPLAY.TOML", .warning = true};
    }
    if (!draft->pending.empty()) {
      return hostui::MenuStatus{.text = "UNSAVED CHANGES", .warning = draft->leave_warned};
    }
    if (draft->applied) return hostui::MenuStatus{.text = "SETTINGS SAVED AND APPLIED."};
    return hostui::MenuStatus{};
  });
  screen->set_update([draft](bool closing) {
    if (closing && !draft->pending.empty()) REXLOG_INFO("Gameplay page: discarded an unapplied change");
  });
  return screen;
}

}  // namespace

void SetSettingsTabsChanged(std::function<void()> changed) {
  std::lock_guard lock(g_tabs_changed_mutex);
  g_tabs_changed = std::move(changed);
}

std::vector<SettingsPageInfo> SettingsPages() {
  const SettingsLayout& layout = ActiveSettingsLayout();
  std::vector<SettingsPageInfo> pages{{layout.game.title, false}};
  if (ModsTabUsable() && REXCVAR_GET(fm4_mod_menu)) pages.push_back({layout.mod.title, true});
  return pages;
}

std::unique_ptr<MenuScreen> CreateSettingsPage(bool mod, config::HostConfig& config) {
  const SettingsLayout& layout = ActiveSettingsLayout();
  return CreatePage(mod ? layout.mod : layout.game, config);
}

std::unique_ptr<MenuScreen> CreateGameplayPage(config::HostConfig& config) {
  static const SettingsTab all = EverySettingOnOnePage();
  return CreatePage(all, config);
}

void ApplySavedGameplaySettings(const config::HostConfig& config) {
  if (const auto value = config.Get(kLaps)) {
    if (FromCommandLine(kLaps)) {
      REXLOG_INFO("Gameplay settings: {} comes from the command line, not {}", kLaps, config.path().string());
    } else if (const auto laps = ParseLaps(*value); !laps || !rex::cvar::SetFlagByName(kLaps, std::to_string(*laps))) {
      REXLOG_WARN("Gameplay settings: ignored {} = {} in {}", kLaps, *value, config.path().string());
    } else {
      REXLOG_INFO("Gameplay settings: {} = {}", kLaps, *laps);
    }
  }
  if (const auto value = config.Get(kModMenu); value && !FromCommandLine(kModMenu)) {
    if ((*value == "true" || *value == "false") && rex::cvar::SetFlagByName(kModMenu, *value)) {
      REXLOG_INFO("Gameplay settings: {} = {}", kModMenu, *value);
    } else {
      REXLOG_WARN("Gameplay settings: ignored {} = {}", kModMenu, *value);
    }
  }
}

}  // namespace fm4_static_recomp::ui
