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

#include "hardcore.h"
#include "quick_race_laps.h"

namespace fm4_static_recomp::ui {
namespace {

using hostui::MenuRow;
using hostui::MenuScreen;

constexpr const char* kLaps = "fm4_quick_race_laps";

// The DIFFICULTY settings (src/hardcore.h), each a few choices, the first the default.
struct Choice {
  const char* value;  // as fm4_* takes it
  const char* label;
};
struct HardcoreSetting {
  const char* name;
  const char* label;
  const char* description;
  std::vector<Choice> choices;
};

const std::vector<HardcoreSetting>& HardcoreSettings() {
  static const std::vector<HardcoreSetting> settings = {
      {"fm4_race_payouts", "RACE PAYOUTS",
       "The credits a race pays by distance, and the bonuses for each car maker's affinity "
       "level. -25% and -50% pay that much less than the default (a mod's own included). From "
       "the next start.",
       {{"100", "DEFAULT"}, {"75", "-25%"}, {"50", "-50%"}}},
      {"fm4_repair_costs", "REPAIR COSTS",
       "What damage costs you from a race's winnings. +100% and +200% make each damaged part "
       "cost that much more than the default, a part at most the whole winnings. From the "
       "next start.",
       {{"100", "DEFAULT"}, {"200", "+100%"}, {"300", "+200%"}}},
      {"fm4_resale_value", "RESALE VALUE",
       "What selling a car pays. -25% and -50% pay that much less than the default (a mod's "
       "own included). From the next start.",
       {{"100", "DEFAULT"}, {"75", "-25%"}, {"50", "-50%"}}},
  };
  return settings;
}

int ChoiceIndex(const HardcoreSetting& setting, std::string_view value) {
  for (size_t i = 0; i < setting.choices.size(); ++i) {
    if (setting.choices[i].value == value) return int(i);
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
      } else {
        // A DIFFICULTY setting; the default (100) is no line at all.
        if (value == "100") {
          config.Remove(name);
        } else {
          config.Set(name, value);
        }
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

MenuRow Heading(const char* label) {
  MenuRow row;
  row.label = label;
  row.heading = true;
  row.divider_above = true;
  return row;
}

MenuRow HardcoreRow(const std::shared_ptr<Draft>& draft, const HardcoreSetting& setting) {
  MenuRow row;
  row.label = setting.label;
  row.description = setting.description;
  row.value = [draft, &setting] {
    const int index = ChoiceIndex(setting, draft->Value(setting.name));
    return std::string(setting.choices[size_t(std::max(index, 0))].label);
  };
  row.adjust = [draft, &setting](int direction) {
    const int count = int(setting.choices.size());
    const int index = (std::max(ChoiceIndex(setting, draft->Value(setting.name)), 0) + direction + count) % count;
    draft->Change(setting.name, setting.choices[size_t(index)].value);
  };
  // The game's data was read as it started.
  row.note = [draft, &setting] {
    // A launch option wins at every start; a value out of range counts as the default.
    if (FromCommandLine(setting.name)) return std::string();
    const int now = std::max(ChoiceIndex(setting, draft->Value(setting.name)), 0);
    const int at_start = std::max(ChoiceIndex(setting, HardcoreSettingAtStart(setting.name)), 0);
    return std::string(now != at_start ? "From the next start." : "");
  };
  row.badge = [&setting] { return LaunchOptionBadge(setting.name); };
  row.unapplied = [draft, &setting] { return draft->pending.contains(setting.name); };
  return row;
}

}  // namespace

std::unique_ptr<MenuScreen> CreateGameplayPage(config::HostConfig& config) {
  auto draft = std::make_shared<Draft>(config);
  std::vector<MenuRow> rows;
  rows.push_back(LapsRow(draft));
  rows.push_back(Heading("DIFFICULTY"));
  for (const HardcoreSetting& setting : HardcoreSettings()) rows.push_back(HardcoreRow(draft, setting));
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
  for (const HardcoreSetting& setting : HardcoreSettings()) {
    const auto value = config.Get(setting.name);
    if (!value || FromCommandLine(setting.name)) continue;
    if (ChoiceIndex(setting, *value) >= 0 && rex::cvar::SetFlagByName(setting.name, *value)) {
      REXLOG_INFO("Gameplay settings: {} = {}", setting.name, *value);
    } else {
      REXLOG_WARN("Gameplay settings: ignored {} = {}", setting.name, *value);
    }
  }
}

}  // namespace fm4_static_recomp::ui
