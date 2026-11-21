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
#include <string_view>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "custom_decals.h"
#include "quick_race_laps.h"

REXCVAR_DEFINE_STRING(fm4_custom_decals_folder, "", "Forza Motorsport 4",
                      "CUSTOM DECALS: a folder whose PNG images become decals of the livery editor, on "
                      "every car (taken the next time the paint editor opens); empty for none");
REXCVAR_DEFINE_BOOL(fm4_all_sponsor_decals, false, "Forza Motorsport 4",
                    "ALL SPONSOR DECALS: every decal the game has is offered on every car, not only "
                    "the car maker's usual ones (taken the next time the paint editor opens)");

namespace fm4_static_recomp::ui {
namespace {

using hostui::MenuRow;
using hostui::MenuScreen;

constexpr const char* kLaps = "fm4_quick_race_laps";
constexpr const char* kDecalsFolder = "fm4_custom_decals_folder";
constexpr const char* kAllSponsors = "fm4_all_sponsor_decals";

std::mutex g_picker_mutex;
FolderPicker g_picker;

// A folder the picker chose, waiting for the page's next update (the picker answers on its own
// thread).
struct PickedFolder {
  std::mutex mutex;
  std::optional<std::string> folder;
  bool refused = false;  // the last folder chosen had '#' in its path (read only on the page's thread)
};

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
      } else if (name == kDecalsFolder) {
        if (value.empty()) {
          config.Remove(kDecalsFolder);
        } else {
          config.Set(kDecalsFolder, config::Quote(value));
        }
      } else if (name == kAllSponsors) {
        config.Set(kAllSponsors, value);
      }
    }
    if (!config.Save()) {
      REXLOG_ERROR("Gameplay page: could not write {}", config.path().string());
      save_failed = true;
      return;
    }
    bool decals_changed = false;
    for (const auto& [name, value] : pending) {
      rex::cvar::SetFlagByName(name, value);
      REXLOG_INFO("Gameplay page: {} = {}", name, value);
      decals_changed = decals_changed || name == kDecalsFolder || name == kAllSponsors;
    }
    if (decals_changed) {
      decals::SetSettings({REXCVAR_GET(fm4_custom_decals_folder), REXCVAR_GET(fm4_all_sponsor_decals)});
      decals::ApplyCustomDecals();
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

std::string FolderLabel(const std::string& folder) {
  if (folder.empty()) return "NONE";
  const std::string name = rex::path_to_utf8(rex::to_path(folder).filename());
  return name.empty() ? folder : name;
}

// What the decals of the applied settings came to. A problem that is a sentence (ends with '.')
// stands alone; the others name an image: "<file> (<why>)".
std::string DecalsNote() {
  const decals::Status status = decals::CurrentStatus();
  if (status.building) {
    return "Preparing your decals...";
  }
  std::string note;
  if (status.decals > 0) {
    note = std::to_string(status.decals) + (status.decals == 1 ? " custom decal" : " custom decals") + " ready.";
  } else if (status.problems.empty()) {
    note = "This folder has no PNG images yet.";
  }
  std::vector<std::string> images;
  for (const std::string& problem : status.problems) {
    if (problem.ends_with('.')) {
      note += (note.empty() ? "" : " ") + problem;
    } else {
      images.push_back(problem);
    }
  }
  for (size_t i = 0; i < images.size(); ++i) {
    note += (note.empty() ? "" : (i ? ", " : " ")) + std::string(i ? "" : "Couldn't use: ") + images[i];
  }
  if (!images.empty()) {
    note += ".";
  }
  return note;
}

MenuRow DecalsFolderRow(const std::shared_ptr<Draft>& draft, const std::shared_ptr<PickedFolder>& picked) {
  MenuRow row;
  row.label = "DECALS FOLDER";
  row.description =
      "Put your own logos and graphics on any car. Choose a folder of PNG images: each one becomes "
      "a decal, named after its file. In the livery editor, create a layer and choose APPLY A "
      "CUSTOM DECAL. Logos on a transparent background look best. Images you add or change later "
      "appear the next time you open the editor. Press A to choose a folder, or LEFT to stop using "
      "it.";
  row.value = [draft] { return FolderLabel(draft->Value(kDecalsFolder)); };
  row.adjust = [draft, picked](int direction) {
    if (direction < 0) {
      picked->refused = false;
      draft->Change(kDecalsFolder, "");
      return;
    }
    std::lock_guard lock(g_picker_mutex);
    if (!g_picker) return;
    g_picker(draft->Value(kDecalsFolder), [picked](std::optional<std::string> folder) {
      std::lock_guard picked_lock(picked->mutex);
      picked->folder = std::move(folder);
    });
  };
  row.note = [draft, picked] {
    if (picked->refused) {
      return std::string("This folder can't be used because its location contains a '#'. Rename the folder, "
                         "or choose a different one.");
    }
    const bool applied = !draft->pending.contains(kDecalsFolder);
    return applied && !draft->Value(kDecalsFolder).empty() ? DecalsNote() : std::string();
  };
  row.badge = [] { return LaunchOptionBadge(kDecalsFolder); };
  row.unapplied = [draft] { return draft->pending.contains(kDecalsFolder); };
  return row;
}

MenuRow AllSponsorsRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "ALL SPONSOR DECALS";
  row.description =
      "Each car normally offers its manufacturer's logo and a selection of sponsors. Turn this on "
      "to offer every sponsor decal in the game on every car, listed after the usual ones.";
  row.value = [draft] { return std::string(draft->Value(kAllSponsors) == "true" ? "ON" : "OFF"); };
  row.adjust = [draft](int) {
    draft->Change(kAllSponsors, draft->Value(kAllSponsors) == "true" ? "false" : "true");
  };
  row.badge = [] { return LaunchOptionBadge(kAllSponsors); };
  row.unapplied = [draft] { return draft->pending.contains(kAllSponsors); };
  return row;
}

}  // namespace

void SetFolderPicker(FolderPicker picker) {
  std::lock_guard lock(g_picker_mutex);
  g_picker = std::move(picker);
}

std::unique_ptr<MenuScreen> CreateGameplayPage(config::HostConfig& config) {
  auto draft = std::make_shared<Draft>(config);
  auto picked = std::make_shared<PickedFolder>();
  std::vector<MenuRow> rows;
  rows.push_back(LapsRow(draft));
  rows.push_back(Heading("CUSTOM DECALS"));
  rows.push_back(DecalsFolderRow(draft, picked));
  rows.push_back(AllSponsorsRow(draft));
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
  screen->set_update([draft, picked](bool closing) {
    std::optional<std::string> folder;
    {
      std::lock_guard lock(picked->mutex);
      folder.swap(picked->folder);
    }
    if (folder && !folder->empty()) {
      // Kept with forward slashes (the settings file keeps no backslashes); '#' starts a comment there.
      std::string value = *folder;  // UTF-8
      std::replace(value.begin(), value.end(), '\\', '/');
      picked->refused = value.find('#') != std::string::npos;
      if (picked->refused) {
        REXLOG_WARN("Gameplay page: a decals folder with '#' in its path cannot be kept: {}", value);
      } else {
        draft->Change(kDecalsFolder, value);
      }
    }
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
  if (const auto value = config.Get(kDecalsFolder); value && !FromCommandLine(kDecalsFolder)) {
    rex::cvar::SetFlagByName(kDecalsFolder, *value);
    REXLOG_INFO("Gameplay settings: {} = {}", kDecalsFolder, *value);
  }
  if (const auto value = config.Get(kAllSponsors); value && !FromCommandLine(kAllSponsors)) {
    if (*value == "true" || *value == "false") {
      rex::cvar::SetFlagByName(kAllSponsors, *value);
      REXLOG_INFO("Gameplay settings: {} = {}", kAllSponsors, *value);
    } else {
      REXLOG_WARN("Gameplay settings: ignored {} = {}", kAllSponsors, *value);
    }
  }
}

}  // namespace fm4_static_recomp::ui
