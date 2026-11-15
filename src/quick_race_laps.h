// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fm4_static_recomp {

// fm4_quick_race_laps: 0 keeps each track's own Quick Race lap count (Tracks.QuickRaceLapCount
// in gamedb.slt), 1 to kQuickRaceLapsMax replaces it on every track that has one.
inline constexpr int kQuickRaceLapsMax = 99;

int QuickRaceLapsSetting();

// The lap count race setup takes for a track whose database count is `track_laps`, in game
// mode `mode`. The Kinect modes (1, 0x22) read their own column and keep it; a track without
// a Quick Race count (0) keeps it.
int QuickRaceLaps(int track_laps, uint32_t mode, int setting);

// Track select's list query with its QuickRaceLapCount column giving `setting` laps on the
// tracks that have a count, under the same column name; nullopt for another query or 0.
std::optional<std::string> QuickRaceLapsQuery(std::string_view sql, int setting);

}  // namespace fm4_static_recomp
