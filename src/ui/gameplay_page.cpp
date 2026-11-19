// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "ui/gameplay_page.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "quick_race_laps.h"

REXCVAR_DEFINE_BOOL(fm4_hide_online_items, false, "Forza Motorsport 4",
                    "Leaves the menu items that need Xbox LIVE or the Forza servers off the "
                    "menus (COMMUNITY, MARKETPLACE, RIVALS, BUY TUNING SETUPS, BUY DESIGNS), from "
                    "the next time each menu is built");

namespace fm4_static_recomp::ui {
namespace {

using hostui::MenuRow;
using hostui::MenuScreen;

constexpr const char* kLaps = "fm4_quick_race_laps";
constexpr const char* kHideOnlineItems = "fm4_hide_online_items";

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

std::string CurrentValue(const char* name) {
  return rex::cvar::GetFlagByName(name);
}

// The page's settings: each drafted value (as fm4_* takes it) until SAVE & APPLY.
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
    if (pending.empty()) {
      return;
    }
    for (const auto& [name, value] : pending) {
      if (name == kLaps) {
        if (value == "0") {
          config.Remove(kLaps);
        } else {
          config.Set(kLaps, value);
        }
      } else if (name == kHideOnlineItems) {
        config.Set(kHideOnlineItems, value);
      }
    }
    if (!config.Save()) {
      REXLOG_ERROR("Gameplay page: could not write {}", config.path().string());
      save_failed = true;
      return;
    }
    for (const auto& [name, value] : pending) {
      rex::cvar::SetFlagByName(name, value);
      REXLOG_INFO("Gameplay page: {} = {}", name, value);
    }
    pending.clear();
    leave_warned = false;
    applied = true;
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

MenuRow OnlineItemsRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "ONLINE MENU ITEMS";
  row.divider_above = true;
  row.description =
      "COMMUNITY and MARKETPLACE on the main menu, RIVALS, BUY TUNING SETUPS and BUY DESIGNS "
      "need Xbox LIVE or the Forza servers, so here they only say the service is unavailable. "
      "HIDDEN leaves them off the menus; each menu shows the change the next time it opens.";
  row.value = [draft] {
    return std::string(draft->Value(kHideOnlineItems) == "true" ? "HIDDEN" : "SHOWN");
  };
  row.adjust = [draft](int) {
    draft->Change(kHideOnlineItems, draft->Value(kHideOnlineItems) == "true" ? "false" : "true");
  };
  row.badge = [] { return LaunchOptionBadge(kHideOnlineItems); };
  row.unapplied = [draft] { return draft->pending.contains(kHideOnlineItems); };
  return row;
}

}  // namespace

std::unique_ptr<MenuScreen> CreateGameplayPage(config::HostConfig& config) {
  auto draft = std::make_shared<Draft>(config);
  std::vector<MenuRow> rows;
  rows.push_back(LapsRow(draft));
  rows.push_back(OnlineItemsRow(draft));
  auto screen = std::make_unique<MenuScreen>("GAMEPLAY", std::move(rows));
  screen->set_apply([draft] { draft->SaveAndApply(); });
  screen->set_leave([draft] {
    if (draft->pending.empty() || draft->leave_warned) {
      return true;
    }
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
    if (draft->applied) {
      return hostui::MenuStatus{.text = "SETTINGS SAVED AND APPLIED."};
    }
    return hostui::MenuStatus{};
  });
  screen->set_update([draft](bool closing) {
    if (closing && !draft->pending.empty()) {
      REXLOG_INFO("Gameplay page: discarded an unapplied change");
    }
  });
  return screen;
}

void ApplySavedGameplaySettings(const config::HostConfig& config) {
  if (const auto value = config.Get(kLaps)) {
    if (FromCommandLine(kLaps)) {
      REXLOG_INFO("Gameplay settings: {} comes from the command line, not {}", kLaps,
                  config.path().string());
    } else if (const auto laps = ParseLaps(*value);
               !laps || !rex::cvar::SetFlagByName(kLaps, std::to_string(*laps))) {
      REXLOG_WARN("Gameplay settings: ignored {} = {} in {}", kLaps, *value, config.path().string());
    } else {
      REXLOG_INFO("Gameplay settings: {} = {}", kLaps, *laps);
    }
  }
  if (const auto value = config.Get(kHideOnlineItems); value && !FromCommandLine(kHideOnlineItems)) {
    if (*value == "true" || *value == "false") {
      rex::cvar::SetFlagByName(kHideOnlineItems, *value);
      REXLOG_INFO("Gameplay settings: {} = {}", kHideOnlineItems, *value);
    } else {
      REXLOG_WARN("Gameplay settings: ignored {} = {}", kHideOnlineItems, *value);
    }
  }
}

}  // namespace fm4_static_recomp::ui
