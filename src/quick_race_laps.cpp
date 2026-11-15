// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "quick_race_laps.h"

#include <rex/cvar.h>

REXCVAR_DEFINE_INT32(fm4_quick_race_laps, 0, "Forza Motorsport 4",
                     "Experimental: Quick Race laps on every track, 1 to 99; 0 keeps each "
                     "track's own count")
    .range(0, fm4_static_recomp::kQuickRaceLapsMax);

namespace fm4_static_recomp {

int QuickRaceLapsSetting() { return REXCVAR_GET(fm4_quick_race_laps); }

int QuickRaceLaps(int track_laps, uint32_t mode, int setting) {
  if (setting <= 0 || track_laps <= 0 || mode == 1 || mode == 0x22) {
    return track_laps;
  }
  return setting;
}

std::optional<std::string> QuickRaceLapsQuery(std::string_view sql, int setting) {
  constexpr std::string_view kColumn = ", QuickRaceLapCount FROM Tracks ";
  const size_t at = sql.find(kColumn);
  if (setting <= 0 || at == std::string_view::npos) {
    return std::nullopt;
  }
  // The list reads the column by name, so the replacement keeps it.
  return std::string(sql.substr(0, at)) + ", (CASE WHEN QuickRaceLapCount > 0 THEN " +
         std::to_string(setting) +
         " ELSE QuickRaceLapCount END) AS QuickRaceLapCount FROM Tracks " +
         std::string(sql.substr(at + kColumn.size()));
}

}  // namespace fm4_static_recomp
