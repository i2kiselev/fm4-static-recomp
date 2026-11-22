// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
// How PC OPTIONS lays out its game settings. Nothing here shows in the game but the layout chosen: to try another,
// change kActiveSettingsLayout; to add one, add an entry to SettingsLayouts().
//
// Every setting has its place here, also those of features not in this build yet (open PRs): a row this build
// does not make (ui/gameplay_page.cpp, kRows) is left out, and so is a heading or a tab left with no rows. A
// feature brings its row with one kRows entry; its place is already here.
#pragma once

#include <array>
#include <vector>

namespace fm4_static_recomp::ui {

enum class SettingRow {
  kQuickRaceLaps,
  kQuickRestart,
  kQuickRestartButton,
  kQuickRestartTrigger,
  kNarrator,
  kOnlineItems,
  kFastPostRace,
  kRacePayouts,
  kRepairCosts,
  kResaleValue,
  kAutoRestart,
  kDecalsFolder,
  kAllSponsors,
  kTrackSignage,
  kTrackTextures,
  kRoadSurface,
  kGrassTexture,
  kModMenu,
};

struct SettingsSection {
  const char* heading;  // nullptr: no heading
  std::vector<SettingRow> rows;
};
struct SettingsTab {
  const char* title;
  std::vector<SettingsSection> sections;
};
// `mod` with no rows this build makes: no MODS tab, and no switch for it.
struct SettingsLayout {
  SettingsTab game;
  SettingsTab mod;
};

inline constexpr size_t kActiveSettingsLayout = 0;

inline const std::array<SettingsLayout, 3>& SettingsLayouts() {
  using enum SettingRow;
  static const std::array<SettingsLayout, 3> layouts = {{
      // 0. GAME kept to the game's own; all of our features in MODS, grouped by what they change.
      {{"GAME",
        {{"RACE SETTINGS", {kQuickRaceLaps}},
         {"MODS", {kModMenu}}}},
       {"MODS",
        {{"RACE SETTINGS", {kQuickRestart, kQuickRestartButton, kQuickRestartTrigger}},
         {"DIFFICULTY", {kRacePayouts, kRepairCosts, kResaleValue, kAutoRestart}},
         {"MENUS/UI", {kFastPostRace, kOnlineItems, kNarrator}},
         {"DECALS", {kDecalsFolder, kAllSponsors}},
         {"TRACK CUSTOMISATION", {kTrackSignage, kTrackTextures, kRoadSurface, kGrassTexture}}}}},
      // 1. Quick restart stays with the everyday race settings; the rest in MODS.
      {{"GAME",
        {{"RACE SETTINGS", {kQuickRaceLaps, kQuickRestart, kQuickRestartButton, kQuickRestartTrigger}},
         {"MODS", {kModMenu}}}},
       {"MODS",
        {{"DIFFICULTY", {kRacePayouts, kRepairCosts, kResaleValue, kAutoRestart}},
         {"MENUS/UI", {kFastPostRace, kOnlineItems, kNarrator}},
         {"PAINT", {kDecalsFolder, kAllSponsors}},
         {"TRACK CUSTOMISATION", {kTrackSignage, kTrackTextures, kRoadSurface, kGrassTexture}}}}},
      // 2. MODS named after the game's own menus, so each is where a player expects it.
      {{"GAME",
        {{"RACE SETTINGS", {kQuickRaceLaps}},
         {"MODS", {kModMenu}}}},
       {"MODS",
        {{"GO RACE", {kQuickRestart, kQuickRestartButton, kQuickRestartTrigger}},
         {"DIFFICULTY", {kRacePayouts, kRepairCosts, kResaleValue, kAutoRestart}},
         {"MENUS/UI", {kFastPostRace, kOnlineItems, kNarrator}},
         {"PAINT", {kDecalsFolder, kAllSponsors}},
         {"TRACK CUSTOMISATION", {kTrackSignage, kTrackTextures, kRoadSurface, kGrassTexture}}}}},
  }};
  return layouts;
}

inline const SettingsLayout& ActiveSettingsLayout() { return SettingsLayouts()[kActiveSettingsLayout]; }

// Every row once, in one list (the unit tests).
inline SettingsTab EverySettingOnOnePage() {
  using enum SettingRow;
  return {"GAME",
          {{nullptr,
            {kQuickRaceLaps, kQuickRestart, kQuickRestartButton, kQuickRestartTrigger, kNarrator, kOnlineItems,
             kFastPostRace, kRacePayouts, kRepairCosts, kResaleValue, kDecalsFolder, kAllSponsors, kTrackSignage,
             kModMenu, kAutoRestart, kTrackTextures, kRoadSurface, kGrassTexture}}}};
}

}  // namespace fm4_static_recomp::ui
