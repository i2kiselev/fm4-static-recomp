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
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "quick_race_laps.h"
#include "track_signage.h"

namespace fm4_static_recomp::ui {
namespace {

using hostui::MenuRow;
using hostui::MenuScreen;

constexpr const char* kLaps = "fm4_quick_race_laps";
constexpr const char* kSignage = "fm4_track_signage";

// A saved count the game takes, or nullopt.
std::optional<int> ParseLaps(const std::string& value) {
  const int laps = std::atoi(value.c_str());
  if (std::to_string(laps) != value || laps < 0 || laps > kQuickRaceLapsMax) {
    return std::nullopt;
  }
  return laps;
}

bool FromCommandLine(const char* name = kLaps) {
  return rex::cvar::GetFlagSource(name) == rex::cvar::Source::kCommandLine;
}

bool SignageOn() { return rex::cvar::GetFlagByName(kSignage) != "false"; }

struct Draft {
  explicit Draft(config::HostConfig& config) : config(config) {}

  config::HostConfig& config;
  std::optional<int> laps;  // drafted, not saved yet
  std::optional<bool> signage;  // drafted, not saved yet
  bool leave_warned = false;
  bool applied = false;
  bool save_failed = false;

  int Laps() const { return laps.value_or(QuickRaceLapsSetting()); }
  bool Signage() const { return signage.value_or(SignageOn()); }
  bool Pending() const { return laps.has_value() || signage.has_value(); }

  void ChangeSignage(bool on) {
    if (on == SignageOn()) {
      signage.reset();
    } else {
      signage = on;
    }
    leave_warned = false;
    applied = false;
    save_failed = false;
  }

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
    if (!Pending()) {
      return;
    }
    if (laps) {
      if (*laps) {
        config.Set(kLaps, std::to_string(*laps));
      } else {
        config.Remove(kLaps);
      }
    }
    if (signage) {
      if (*signage) {
        config.Remove(kSignage);  // on is the default
      } else {
        config.Set(kSignage, "false");
      }
    }
    if (!config.Save()) {
      REXLOG_ERROR("Gameplay page: could not write {}", config.path().string());
      save_failed = true;
      return;
    }
    if (laps) {
      rex::cvar::SetFlagByName(kLaps, std::to_string(*laps));
      REXLOG_INFO("Gameplay page: {} = {}", kLaps, *laps);
    }
    if (signage) {
      rex::cvar::SetFlagByName(kSignage, *signage ? "true" : "false");
      REXLOG_INFO("Gameplay page: {} = {}", kSignage, *signage);
    }
    laps.reset();
    signage.reset();
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

MenuRow Heading(const char* label) {
  MenuRow row;
  row.label = label;
  row.heading = true;
  row.divider_above = true;
  return row;
}

MenuRow SignageRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "TRACK SIGNAGE";
  row.description =
      "Your own pictures on the trackside boards: put a PNG named after a board in the signage "
      "folder and races show it on that board, within a few seconds, even mid-race. README.txt "
      "in the folder lists the boards and their sizes; a bigger picture gives a sharper board. "
      "OFF shows the game's boards.";
  row.value = [draft] { return std::string(draft->Signage() ? "ON" : "OFF"); };
  row.adjust = [draft](int) { draft->ChangeSignage(!draft->Signage()); };
  row.note = [draft] {
    if (!draft->Signage() || draft->signage.has_value()) return std::string();
    const size_t shown = signage::ShownCount();
    if (!shown) return std::string("No pictures in the signage folder yet.");
    return std::to_string(shown) + (shown == 1 ? " board shows your picture." : " boards show your pictures.");
  };
  row.badge = [] {
    return FromCommandLine(kSignage) ? std::string("Set by a launch option for this session.") : std::string();
  };
  row.unapplied = [draft] { return draft->signage.has_value(); };
  return row;
}

MenuRow SignageFolderRow() {
  MenuRow row;
  row.label = "SIGNAGE FOLDER";
  row.description =
      "Opens the signage folder in Explorer, to put pictures in or take them out. Name each "
      "picture after its board, as README.txt there lists them (hockenheim-mobil1.png, ...).";
  row.value = [] { return std::string("OPEN"); };
  row.adjust = [](int) { signage::OpenFolder(); };
  row.note = [] {
    const auto folder = signage::Folder();
    return folder.empty() ? std::string() : rex::path_to_utf8(folder);
  };
  return row;
}

}  // namespace

std::unique_ptr<MenuScreen> CreateGameplayPage(config::HostConfig& config) {
  auto draft = std::make_shared<Draft>(config);
  std::vector<MenuRow> rows;
  rows.push_back(LapsRow(draft));
  rows.push_back(Heading("TRACK SIGNAGE"));
  rows.push_back(SignageRow(draft));
  rows.push_back(SignageFolderRow());
  auto screen = std::make_unique<MenuScreen>("GAMEPLAY", std::move(rows));
  screen->set_apply([draft] { draft->SaveAndApply(); });
  screen->set_leave([draft] {
    if (!draft->Pending() || draft->leave_warned) {
      return true;
    }
    draft->leave_warned = true;
    return false;
  });
  screen->set_status([draft] {
    if (draft->save_failed) {
      return hostui::MenuStatus{.text = "COULD NOT SAVE GAMEPLAY.TOML", .warning = true};
    }
    if (draft->Pending()) {
      return hostui::MenuStatus{.text = "UNSAVED CHANGES", .warning = draft->leave_warned};
    }
    if (draft->applied) {
      return hostui::MenuStatus{.text = "SETTINGS SAVED AND APPLIED."};
    }
    return hostui::MenuStatus{};
  });
  screen->set_update([draft](bool closing) {
    if (closing && draft->Pending()) {
      REXLOG_INFO("Gameplay page: discarded an unapplied change");
    }
  });
  return screen;
}

void ApplySavedGameplaySettings(const config::HostConfig& config) {
  if (const auto signage = config.Get(kSignage)) {
    if (FromCommandLine(kSignage)) {
      REXLOG_INFO("Gameplay settings: {} comes from the command line, not {}", kSignage, config.path().string());
    } else if (*signage == "false" || *signage == "true") {
      rex::cvar::SetFlagByName(kSignage, *signage);
      REXLOG_INFO("Gameplay settings: {} = {}", kSignage, *signage);
    } else {
      REXLOG_WARN("Gameplay settings: ignored {} = {} in {}", kSignage, *signage, config.path().string());
    }
  }
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
