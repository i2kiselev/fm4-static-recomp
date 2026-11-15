// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "ui/gameplay_page.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
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

// A saved count the game takes, or nullopt.
std::optional<int> ParseLaps(const std::string& value) {
  const int laps = std::atoi(value.c_str());
  if (std::to_string(laps) != value || laps < 0 || laps > kQuickRaceLapsMax) {
    return std::nullopt;
  }
  return laps;
}

bool FromCommandLine() {
  return rex::cvar::GetFlagSource(kLaps) == rex::cvar::Source::kCommandLine;
}

struct Draft {
  explicit Draft(config::HostConfig& config) : config(config) {}

  config::HostConfig& config;
  std::optional<int> laps;  // drafted, not saved yet
  bool leave_warned = false;
  bool applied = false;
  bool save_failed = false;

  int Laps() const { return laps.value_or(QuickRaceLapsSetting()); }

  void Change(int value) {
    if (value == QuickRaceLapsSetting()) {
      laps.reset();
    } else {
      laps = value;
    }
    leave_warned = false;
    applied = false;
    save_failed = false;
  }

  void SaveAndApply() {
    if (!laps) {
      return;
    }
    if (*laps) {
      config.Set(kLaps, std::to_string(*laps));
    } else {
      config.Remove(kLaps);
    }
    if (!config.Save()) {
      REXLOG_ERROR("Gameplay page: could not write {}", config.path().string());
      save_failed = true;
      return;
    }
    rex::cvar::SetFlagByName(kLaps, std::to_string(*laps));
    REXLOG_INFO("Gameplay page: {} = {}", kLaps, *laps);
    laps.reset();
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
    draft->Change(std::clamp(draft->Laps() + direction, 0, kQuickRaceLapsMax));
  };
  row.badge = [] {
    return FromCommandLine() ? std::string("Set by a launch option for this session.")
                             : std::string();
  };
  row.unapplied = [draft] { return draft->laps.has_value(); };
  return row;
}

}  // namespace

std::unique_ptr<MenuScreen> CreateGameplayPage(config::HostConfig& config) {
  auto draft = std::make_shared<Draft>(config);
  std::vector<MenuRow> rows;
  rows.push_back(LapsRow(draft));
  auto screen = std::make_unique<MenuScreen>("GAMEPLAY", std::move(rows));
  screen->set_apply([draft] { draft->SaveAndApply(); });
  screen->set_leave([draft] {
    if (!draft->laps || draft->leave_warned) {
      return true;
    }
    draft->leave_warned = true;
    return false;
  });
  screen->set_status([draft] {
    if (draft->save_failed) {
      return hostui::MenuStatus{.text = "COULD NOT SAVE GAMEPLAY.TOML", .warning = true};
    }
    if (draft->laps) {
      return hostui::MenuStatus{.text = "UNSAVED CHANGES", .warning = draft->leave_warned};
    }
    if (draft->applied) {
      return hostui::MenuStatus{.text = "SETTINGS SAVED AND APPLIED."};
    }
    return hostui::MenuStatus{};
  });
  screen->set_update([draft](bool closing) {
    if (closing && draft->laps) {
      REXLOG_INFO("Gameplay page: discarded an unapplied change");
    }
  });
  return screen;
}

void ApplySavedGameplaySettings(const config::HostConfig& config) {
  const auto value = config.Get(kLaps);
  if (!value) {
    return;
  }
  if (FromCommandLine()) {
    REXLOG_INFO("Gameplay settings: {} comes from the command line, not {}", kLaps,
                config.path().string());
  } else if (const auto laps = ParseLaps(*value);
             !laps || !rex::cvar::SetFlagByName(kLaps, std::to_string(*laps))) {
    REXLOG_WARN("Gameplay settings: ignored {} = {} in {}", kLaps, *value,
                config.path().string());
  } else {
    REXLOG_INFO("Gameplay settings: {} = {}", kLaps, *laps);
  }
}

}  // namespace fm4_static_recomp::ui
