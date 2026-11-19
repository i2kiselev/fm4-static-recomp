// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "hardcore.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>

#include <rex/cvar.h>
#include <rex/filesystem/content_override.h>
#include <rex/logging.h>

#include <sqlite3.h>

REXCVAR_DEFINE_INT32(fm4_race_payouts, 100, "Forza Motorsport 4",
                     "HARDCORE: race payouts as a percentage of the game's: 100, 75 or 50 (from the "
                     "next start)");
REXCVAR_DEFINE_INT32(fm4_repair_costs, 100, "Forza Motorsport 4",
                     "HARDCORE: repair costs as a percentage of the game's: 100, 200 or 300 (from "
                     "the next start)");
REXCVAR_DEFINE_INT32(fm4_resale_value, 100, "Forza Motorsport 4",
                     "HARDCORE: what selling a car pays, as a percentage of the game's: 100, 75 or "
                     "50 (from the next start)");

namespace fm4_static_recomp {
namespace {

constexpr const char* kDatabaseGuestPath = "game:\\Media\\db\\gamedb.slt";
constexpr const char* kTunablesGuestPath = "game:\\Media\\GameTunableSettings\\GameTunableSettings.ini";
constexpr const char* kSettings[] = {"fm4_race_payouts", "fm4_repair_costs", "fm4_resale_value"};

// The settings as the game started, recorded by InstallHardcoreOptions.
std::map<std::string, std::string, std::less<>> g_at_start;

std::optional<std::vector<uint8_t>> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// The line of `key` ("Section\Key value"): its start and end (before any line break), when the
// key starts exactly one line.
std::optional<std::pair<size_t, size_t>> TunableLine(std::string_view text, std::string_view key) {
  std::optional<std::pair<size_t, size_t>> found;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    size_t content_end = end;
    if (content_end > start && text[content_end - 1] == '\r') --content_end;
    const std::string_view line = text.substr(start, content_end - start);
    if (line.size() > key.size() && line.substr(0, key.size()) == key && line[key.size()] == ' ') {
      if (found) return std::nullopt;  // twice
      found = std::pair{start, content_end};
    }
    start = end + 1;
  }
  return found;
}

}  // namespace

HardcoreEdits RacePayoutEdits(int percent) {
  const std::string f = std::to_string(percent) + " / 100.0";
  return {
      .sql =
          {
              "UPDATE AffinityLevelDef SET BonusCredits = CAST(BonusCredits * " + f + " AS INTEGER)",
          },
      .scales = {{"RaceWinnings\\CreditsPerMile", percent / 100.0, std::nullopt}},
  };
}

HardcoreEdits RepairCostEdits(int percent) {
  HardcoreEdits edits;
  for (const char* part : {"Body", "Brakes", "Clutch", "Driveline", "Engine", "Gearbox", "Steering",
                           "Suspension"}) {
    edits.scales.push_back({std::string("RaceWinnings\\DmgPenalty") + part, percent / 100.0, 1.0});
  }
  return edits;
}

HardcoreEdits ResaleValueEdits(int percent) {
  return {
      .sql = {},
      .scales =
          {
              {"CarValueScales\\SellValueScale", percent / 100.0, std::nullopt},
              {"CarValueScales\\RestockingValueScale", percent / 100.0, std::nullopt},
          },
  };
}

std::vector<HardcoreOption> ActiveHardcoreOptions() {
  std::vector<HardcoreOption> options;
  const auto setting = [](const char* name, std::initializer_list<int> allowed) -> std::optional<int> {
    const int value = std::atoi(rex::cvar::GetFlagByName(name).c_str());
    if (std::find(allowed.begin(), allowed.end(), value) == allowed.end()) {
      REXLOG_WARN("HARDCORE: {} = {} is not one of its settings; the game's is kept", name, value);
      return std::nullopt;
    }
    return value;
  };
  if (const auto v = setting("fm4_race_payouts", {100, 75, 50}); v && *v != 100) {
    options.push_back({"RACE PAYOUTS " + std::to_string(*v) + "%", RacePayoutEdits(*v)});
  }
  if (const auto v = setting("fm4_repair_costs", {100, 200, 300}); v && *v != 100) {
    options.push_back({"REPAIR COSTS " + std::to_string(*v) + "%", RepairCostEdits(*v)});
  }
  if (const auto v = setting("fm4_resale_value", {100, 75, 50}); v && *v != 100) {
    options.push_back({"RESALE VALUE " + std::to_string(*v) + "%", ResaleValueEdits(*v)});
  }
  return options;
}

std::optional<std::string> SetTunable(std::string text, std::string_view key, std::string_view value) {
  const auto line = TunableLine(text, key);
  if (!line) return std::nullopt;
  const auto [start, end] = *line;
  std::string replacement = std::string(key) + ' ' + std::string(value);
  if (replacement.size() > end - start) return std::nullopt;
  replacement.resize(end - start, ' ');
  text.replace(start, end - start, replacement);
  return text;
}

std::optional<std::string> GetTunable(std::string_view text, std::string_view key) {
  const auto line = TunableLine(text, key);
  if (!line) return std::nullopt;
  std::string_view value = text.substr(line->first + key.size() + 1, line->second - line->first - key.size() - 1);
  while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
  while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
  return std::string(value);
}

std::optional<std::string> FormatToFit(double value, size_t width, bool whole, double toward) {
  if (!std::isfinite(value) || value < 0) return std::nullopt;
  for (int decimals = whole ? 0 : 6; decimals >= 0; --decimals) {
    // The nearest value with this many decimals; exactly between two, the one nearer `toward`.
    const double step = std::pow(10.0, -decimals);
    const double low = std::floor(value / step + 1e-9) * step;
    const double high = low + step;
    const double gap_low = value - low, gap_high = high - value;
    double chosen = gap_low < gap_high - 1e-9 ? low : gap_high < gap_low - 1e-9 ? high
                    : std::fabs(low - toward) < std::fabs(high - toward)  ? low
                                                                            : high;
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), chosen, std::chars_format::fixed, decimals);
    std::string text(buffer, result.ptr);
    if (text.find('.') != std::string::npos) {
      while (text.back() == '0') text.pop_back();
      if (text.back() == '.') text.pop_back();
    }
    if (text.size() <= width) return text;
  }
  return std::nullopt;
}

std::optional<std::string> ScaleTunable(std::string text, const TunableScale& scale) {
  const auto old_text = GetTunable(text, scale.key);
  if (!old_text || old_text->empty()) return std::nullopt;
  double old_value = 0;
  const auto parsed = std::from_chars(old_text->data(), old_text->data() + old_text->size(), old_value);
  if (parsed.ec != std::errc() || parsed.ptr != old_text->data() + old_text->size()) return std::nullopt;
  double value = old_value * scale.factor;
  if (scale.max) value = std::min(value, *scale.max);
  const bool whole = old_text->find('.') == std::string::npos;
  const auto formatted = FormatToFit(value, old_text->size(), whole, old_value);
  if (!formatted) return std::nullopt;
  // Written as it was: the option would change nothing.
  double written = 0;
  std::from_chars(formatted->data(), formatted->data() + formatted->size(), written);
  if (std::fabs(written - old_value) < 1e-9 && std::fabs(value - old_value) > 1e-9) return std::nullopt;
  return SetTunable(std::move(text), scale.key, *formatted);
}

std::optional<std::vector<uint8_t>> EditDatabase(const std::vector<uint8_t>& bytes,
                                                 const std::vector<std::string>& sql,
                                                 std::string* error) {
  const auto fail = [error](std::string message) -> std::optional<std::vector<uint8_t>> {
    if (error) *error = std::move(message);
    return std::nullopt;
  };
  if (bytes.empty()) return fail("empty database");
  sqlite3* db = nullptr;
  if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
    sqlite3_close(db);
    return fail("cannot open SQLite");
  }
  // SQLite takes the copy and frees it with the connection.
  auto* copy = static_cast<unsigned char*>(sqlite3_malloc64(bytes.size()));
  if (!copy) {
    sqlite3_close(db);
    return fail("out of memory");
  }
  std::memcpy(copy, bytes.data(), bytes.size());
  if (sqlite3_deserialize(db, "main", copy, sqlite3_int64(bytes.size()), sqlite3_int64(bytes.size()),
                          SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_RESIZEABLE) != SQLITE_OK) {
    std::string message = sqlite3_errmsg(db);
    sqlite3_close(db);
    return fail("not a database: " + message);
  }
  for (const std::string& statement : sql) {
    char* message = nullptr;
    if (sqlite3_exec(db, statement.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
      std::string text = message ? message : "error";
      sqlite3_free(message);
      sqlite3_close(db);
      return fail(text + " (" + statement + ")");
    }
  }
  sqlite3_int64 size = 0;
  unsigned char* out = sqlite3_serialize(db, "main", &size, 0);
  if (!out) {
    sqlite3_close(db);
    return fail("cannot serialize");
  }
  std::vector<uint8_t> result(out, out + size);
  sqlite3_free(out);
  sqlite3_close(db);
  if (result.size() != bytes.size()) {
    return fail("the database changed size (" + std::to_string(bytes.size()) + " to " +
                std::to_string(result.size()) + " bytes)");
  }
  return result;
}

void InstallHardcoreOptions(const std::filesystem::path& game_data_root) {
  g_at_start.clear();
  for (const char* name : kSettings) g_at_start[name] = rex::cvar::GetFlagByName(name);
  const std::vector<HardcoreOption> options = ActiveHardcoreOptions();
  if (options.empty()) return;

  const auto database_path = game_data_root / "Media" / "db" / "gamedb.slt";
  const auto tunables_path = game_data_root / "Media" / "GameTunableSettings" / "GameTunableSettings.ini";
  std::optional<std::vector<uint8_t>> database;
  std::optional<std::string> tunables;
  bool database_changed = false;
  bool tunables_changed = false;

  for (const HardcoreOption& option : options) {
    // Each option is made whole or not at all.
    std::optional<std::vector<uint8_t>> next_database;
    std::optional<std::string> next_tunables;
    std::string problem;
    std::string changes;
    if (!option.edits.sql.empty()) {
      if (!database) database = ReadFile(database_path);
      if (!database) {
        problem = "cannot read " + database_path.string();
      } else {
        next_database = EditDatabase(*database, option.edits.sql, &problem);
      }
    }
    if (problem.empty() && !option.edits.scales.empty()) {
      if (!tunables) {
        if (const auto bytes = ReadFile(tunables_path)) tunables = std::string(bytes->begin(), bytes->end());
      }
      if (!tunables) {
        problem = "cannot read " + tunables_path.string();
      } else {
        next_tunables = *tunables;
        for (const TunableScale& scale : option.edits.scales) {
          const auto before = GetTunable(*next_tunables, scale.key);
          next_tunables = ScaleTunable(std::move(*next_tunables), scale);
          if (!next_tunables) {
            problem = "the tunable " + scale.key + " is missing or cannot be scaled";
            break;
          }
          changes += " " + scale.key + " " + before.value_or("?") + " -> " +
                     GetTunable(*next_tunables, scale.key).value_or("?") + ";";
        }
      }
    }
    if (!problem.empty()) {
      REXLOG_WARN("HARDCORE: {} not applied: {}", option.name, problem);
      continue;
    }
    if (next_database) {
      database = std::move(next_database);
      database_changed = true;
    }
    if (next_tunables) {
      tunables = std::move(next_tunables);
      tunables_changed = true;
    }
    REXLOG_INFO("HARDCORE: {} ({} database statements){}", option.name, option.edits.sql.size(), changes);
  }
  if (database_changed) rex::filesystem::SetContentOverride(kDatabaseGuestPath, std::move(*database));
  if (tunables_changed) {
    rex::filesystem::SetContentOverride(kTunablesGuestPath,
                                        std::vector<uint8_t>(tunables->begin(), tunables->end()));
  }
}

std::string HardcoreSettingAtStart(std::string_view name) {
  if (const auto found = g_at_start.find(name); found != g_at_start.end()) return found->second;
  return rex::cvar::GetFlagByName(std::string(name));
}

}  // namespace fm4_static_recomp
