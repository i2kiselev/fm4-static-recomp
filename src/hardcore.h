// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fm4_static_recomp {

// DIFFICULTY (named Hardcore in the code): options that change the game's own data as it loads it, each only ever making the
// game harder. The game reads its database (Media\db\gamedb.slt) and its tunables
// (Media\GameTunableSettings\GameTunableSettings.ini) read-only at startup; with an option set,
// an edited copy of either is served in place of the file (a content override), so the files on
// disk are never changed and each option takes effect from the next start. Every change is made
// relative to the values the files hold (a mod's own included).

// A tunable scaled by `factor`, kept at or below `max` when `max` is set.
struct TunableScale {
  std::string key;  // "Section\Key"
  double factor = 1.0;
  std::optional<double> max;
  bool decimal = false;  // a factor: written with decimals even where the file has a whole number ("1")
};

// What one option changes.
struct HardcoreEdits {
  std::vector<std::string> sql;      // statements on gamedb.slt
  std::vector<TunableScale> scales;  // tunables
};

struct HardcoreOption {
  std::string name;  // for the log
  HardcoreEdits edits;
};

// RACE PAYOUTS at `percent` of the game's (fm4_race_payouts: 100, 75 or 50): the credits a race
// pays by distance (RaceWinnings\CreditsPerMile) and the affinity level bonuses
// (AffinityLevelDef.BonusCredits). (The database's EventScoring table is not read by the game.)
HardcoreEdits RacePayoutEdits(int percent);
// REPAIR COSTS at `percent` of the game's (fm4_repair_costs: 100, 200 or 300): the share of the
// winnings each damaged part costs (RaceWinnings\DmgPenalty*), each at most the whole.
HardcoreEdits RepairCostEdits(int percent);
// RESALE VALUE at `percent` of the game's (fm4_resale_value: 100, 75 or 50): what selling a car
// pays (CarValueScales\SellValueScale, RestockingValueScale).
HardcoreEdits ResaleValueEdits(int percent);

// The options set now (fm4_* settings), in a fixed order. A setting out of its range is ignored.
std::vector<HardcoreOption> ActiveHardcoreOptions();

// Sets the value of `key` ("Section\Key") in the tunables text, one "Section\Key value" a line,
// keeping the text's length (a content override must match the file's size): the line is
// padded with spaces after the value. nullopt when the key is not found exactly once or the new
// value does not fit in its line.
std::optional<std::string> SetTunable(std::string text, std::string_view key, std::string_view value);
// The value of `key` in the tunables text, as written.
std::optional<std::string> GetTunable(std::string_view text, std::string_view key);
// Scales `key`'s value as `scale` says, written in no more characters than the old value had
// (fewer decimals when needed; whole numbers stay whole). nullopt when it is missing or not a
// number.
std::optional<std::string> ScaleTunable(std::string text, const TunableScale& scale,
                                       std::string* why = nullptr);
// `value` in at most `width` characters, or nullopt: rounded to the nearest whole number when
// `whole`, else to the most decimals that fit, without trailing zeros. Exactly between two, the
// one nearer `toward` (the game's own value: the gentler of the two).
std::optional<std::string> FormatToFit(double value, size_t width, bool whole, double toward);

// Runs `sql` on a copy of the SQLite database `bytes`, in memory. nullopt, with `error` set,
// when a statement fails or the result is not the same size as `bytes`.
std::optional<std::vector<uint8_t>> EditDatabase(const std::vector<uint8_t>& bytes,
                                                 const std::vector<std::string>& sql,
                                                 std::string* error);

// At startup, before the game opens either file: serves the edited copies for the options set
// and logs what each changed. An option whose edits cannot be made is left out, with a warning.
void InstallHardcoreOptions(const std::filesystem::path& game_data_root);

// A DIFFICULTY setting's value as the game started (what this session's data has), for the
// "from the next start" notes; its value now before InstallHardcoreOptions ran.
std::string HardcoreSettingAtStart(std::string_view name);

}  // namespace fm4_static_recomp
