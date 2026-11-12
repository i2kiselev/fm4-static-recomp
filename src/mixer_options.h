// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace fm4_static_recomp {

struct MixerPatchResult {
  std::string text;
  bool crash_applied = false;
  bool filter_applied = false;
};

// Patches Media/audio/AudioMixerSnapshots.xml without changing its length: the crash
// snapshot no longer ducks the race music, and the music zones' high-pass goes from
// 200 to 50 Hz. A change whose text is not found exactly once (a modded file) is skipped.
MixerPatchResult PatchMixerSnapshots(std::string_view xml, bool keep_music_on_crash,
                                     bool light_filter);

// Registers the patched file for the guest when MUSIC ON CRASH or MUSIC FILTER is set.
void InstallMixerOptions(const std::filesystem::path& game_data_root);

}  // namespace fm4_static_recomp
