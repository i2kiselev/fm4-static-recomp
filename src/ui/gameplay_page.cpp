// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "ui/gameplay_page.h"

#include <algorithm>
#include <array>
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

namespace fm4_static_recomp::ui {
namespace {

using hostui::MenuRow;
using hostui::MenuScreen;

constexpr const char* kLaps = "fm4_quick_race_laps";
constexpr const char* kQuickRestart = "fm4_quick_restart";
constexpr const char* kRestartTrigger = "fm4_quick_restart_trigger";

// The pad buttons QUICK RESTART can use, as fm4_quick_restart names them, and as shown: the
// D-pad's left and right, which every controller layout leaves unused while driving, so the
// button does nothing else.
constexpr std::array<std::pair<std::string_view, std::string_view>, 2> kButtons = {{
    {"dpad_left", "D-PAD LEFT"},
    {"dpad_right", "D-PAD RIGHT"},
}};
constexpr std::string_view kDefaultButton = "dpad_left";

int ButtonIndex(std::string_view name) {
  for (size_t i = 0; i < kButtons.size(); ++i) {
    if (kButtons[i].first == name) return int(i);
  }
  return -1;
}

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
  // The button QUICK RESTART goes back to when it is switched on again.
  std::string last_button;
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

  std::string RestartButton() const {
    const std::string value = Value(kQuickRestart);
    if (ButtonIndex(value) >= 0) return value;
    if (ButtonIndex(last_button) >= 0) return last_button;
    return std::string(kDefaultButton);
  }
  bool RestartOn() const { return ButtonIndex(Value(kQuickRestart)) >= 0; }

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
      } else if (name == kQuickRestart) {
        config.Set(kQuickRestart, "\"" + value + "\"");
      } else if (name == kRestartTrigger) {
        config.Set(kRestartTrigger, "\"" + value + "\"");
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

MenuRow QuickRestartRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "QUICK RESTART";
  row.divider_above = true;
  row.description =
      "Hold the QUICK RESTART BUTTON for a moment while racing to restart the race at once: "
      "the screen fades to black and comes back on the countdown, with no menus and a short "
      "start. Only while racing, never in menus or during a countdown.";
  row.value = [draft] { return std::string(draft->RestartOn() ? "ON" : "OFF"); };
  row.adjust = [draft](int) {
    if (draft->RestartOn()) {
      draft->last_button = draft->RestartButton();
      draft->Change(kQuickRestart, "off");
    } else {
      draft->Change(kQuickRestart, draft->RestartButton());
    }
  };
  row.badge = [] { return LaunchOptionBadge(kQuickRestart); };
  row.unapplied = [draft] { return draft->pending.contains(kQuickRestart); };
  return row;
}

MenuRow QuickRestartButtonRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "QUICK RESTART BUTTON";
  row.description =
      "The pad button for QUICK RESTART: the D-pad's left or right, which no controller layout "
      "uses while driving, so it does nothing else. An extra button on your pad mapped to one "
      "of them makes a dedicated restart button.";
  row.value = [draft] {
    return std::string(kButtons[size_t(std::max(ButtonIndex(draft->RestartButton()), 0))].second);
  };
  row.adjust = [draft](int direction) {
    const int count = int(kButtons.size());
    const int index = (ButtonIndex(draft->RestartButton()) + direction + count) % count;
    draft->last_button = std::string(kButtons[size_t(index)].first);
    if (draft->RestartOn()) draft->Change(kQuickRestart, draft->last_button);
  };
  row.enabled = [draft] { return draft->RestartOn(); };
  row.unapplied = [draft] { return draft->pending.contains(kQuickRestart); };
  return row;
}

MenuRow RestartTriggerRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "QUICK RESTART TRIGGER";
  row.description =
      "HOLD: hold the button while the screen fades to black; let go during the fade to cancel. "
      "TAP: one press restarts, after a short fade, with no cancel.";
  row.value = [draft] { return std::string(draft->Value(kRestartTrigger) == "tap" ? "TAP" : "HOLD"); };
  row.adjust = [draft](int) {
    draft->Change(kRestartTrigger, draft->Value(kRestartTrigger) == "tap" ? "hold" : "tap");
  };
  row.enabled = [draft] { return draft->RestartOn(); };
  row.badge = [] { return LaunchOptionBadge(kRestartTrigger); };
  row.unapplied = [draft] { return draft->pending.contains(kRestartTrigger); };
  return row;
}

}  // namespace

std::unique_ptr<MenuScreen> CreateGameplayPage(config::HostConfig& config) {
  auto draft = std::make_shared<Draft>(config);
  std::vector<MenuRow> rows;
  rows.push_back(LapsRow(draft));
  rows.push_back(QuickRestartRow(draft));
  rows.push_back(QuickRestartButtonRow(draft));
  rows.push_back(RestartTriggerRow(draft));
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
  if (const auto value = config.Get(kQuickRestart); value && !FromCommandLine(kQuickRestart)) {
    if (*value == "off" || ButtonIndex(*value) >= 0) {
      rex::cvar::SetFlagByName(kQuickRestart, *value);
      REXLOG_INFO("Gameplay settings: {} = {}", kQuickRestart, *value);
    } else {
      REXLOG_WARN("Gameplay settings: ignored {} = {}", kQuickRestart, *value);
    }
  }
  if (const auto value = config.Get(kRestartTrigger); value && !FromCommandLine(kRestartTrigger)) {
    rex::cvar::SetFlagByName(kRestartTrigger, *value == "tap" ? "tap" : "hold");
  }
}

}  // namespace fm4_static_recomp::ui
