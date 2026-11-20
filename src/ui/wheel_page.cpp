// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "ui/wheel_page.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "wheel_settings.h"

namespace fm4_static_recomp::ui {
namespace {

using hostui::MenuRow;
using hostui::MenuScreen;

constexpr std::string_view kCustom = "custom";
constexpr std::string_view kScaleKey = "wheel_steering_scale";

bool IsTypeKey(std::string_view key) {
  const auto& values = WheelTypes()[0].values;
  return std::ranges::any_of(values, [key](const auto& value) { return value.first == key; });
}

std::string_view CvarOf(std::string_view key) {
  return key == kSteeringLockKey ? kScaleKey : key;
}

bool FromCommandLine(std::string_view key) {
  return rex::cvar::GetFlagSource(CvarOf(key)) == rex::cvar::Source::kCommandLine;
}

struct Draft {
  explicit Draft(config::HostConfig& config) : config(config) { Reset(); }

  config::HostConfig& config;
  std::string type;
  std::map<std::string, int, std::less<>> values;
  // What the game runs now: a launch option's value, else the file's.
  std::map<std::string, int, std::less<>> running;
  bool replace_warned = false;
  bool leave_warned = false;
  bool applied = false;
  bool save_failed = false;

  int Running(std::string_view key) const {
    if (!FromCommandLine(key)) {
      return WheelValue(config, key);
    }
    const int live = rex::cvar::Query<int32_t>(CvarOf(key));
    return key == kSteeringLockKey ? SteeringLockDegrees(WheelRotation(config), live) : live;
  }

  void Reset() {
    type = WheelTypeOf(config);
    for (const WheelRow& row : WheelRows()) {
      running[std::string(row.key)] = values[std::string(row.key)] = Running(row.key);
    }
  }

  bool Unapplied(std::string_view key) const {
    return values.find(key)->second != running.find(key)->second;
  }
  bool Changed() const {
    return type != WheelTypeOf(config) ||
           std::ranges::any_of(WheelRows(), [this](const WheelRow& row) {
             return Unapplied(row.key);
           });
  }
  bool ReplacesCustom() const { return type != kCustom && WheelTypeOf(config) == kCustom; }

  void Touched() {
    replace_warned = false;
    leave_warned = false;
    applied = false;
    save_failed = false;
  }

  void Change(std::string_view key, int value) {
    values.find(key)->second = value;
    if (IsTypeKey(key)) {
      type = kCustom;
    }
    Touched();
  }

  void ChangeType(int direction) {
    const auto types = WheelTypes();
    const auto current =
        std::ranges::find_if(types, [this](const WheelType& t) { return t.id == type; });
    const int count = int(types.size());
    const int index = current == types.end()
                          ? (direction > 0 ? 0 : count - 1)
                          : (int(current - types.begin()) + direction + count) % count;
    type = types[index].id;
    for (const auto& [key, value] : types[index].values) {
      values.find(key)->second = value;
    }
    Touched();
  }

  void SaveAndApply() {
    if (!Changed()) {
      return;
    }
    if (ReplacesCustom() && !replace_warned) {
      replace_warned = true;
      return;
    }
    std::vector<std::string_view> changed;
    for (const WheelRow& row : WheelRows()) {
      if (Unapplied(row.key)) {
        changed.push_back(row.key);
      }
    }
    const bool type_chosen = type != kCustom && type != WheelTypeOf(config);
    // The launcher may have saved the file while the game ran: only the player's changes go on top.
    if (!config.Load()) {
      REXLOG_ERROR("Wheel page: could not read {}", config.path().string());
      save_failed = true;
      return;
    }
    if (type_chosen) {
      SetWheelType(config, type);
    }
    for (const std::string_view key : changed) {
      if (values.find(key)->second != WheelValue(config, key)) {
        SetWheelValue(config, key, values.find(key)->second);
      }
    }
    if (!config.Save()) {
      REXLOG_ERROR("Wheel page: could not write {}", config.path().string());
      save_failed = true;
      return;
    }
    // A launch option the player did not change stays in force for the session.
    for (const std::string_view key : changed) {
      const std::string value = key == kSteeringLockKey ? config.Get(kScaleKey).value_or("100")
                                                        : std::to_string(WheelValue(config, key));
      rex::cvar::SetFlagByName(CvarOf(key), value);
    }
    REXLOG_INFO("Wheel page: wheel type {}, saved and applied", WheelTypeOf(config));
    Reset();
    replace_warned = false;
    leave_warned = false;
    applied = true;
  }
};

std::string TypeLabel(std::string_view id) {
  for (const WheelType& type : WheelTypes()) {
    if (type.id == id) {
      return std::string(type.label);
    }
  }
  return "CUSTOM";
}

std::string LaunchOptionBadge(std::string_view key) {
  return FromCommandLine(key) ? std::string("Set by a launch option for this session.")
                              : std::string();
}

MenuRow TypeRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "WHEEL TYPE";
  row.description = "Pick the kind of wheel you have: it sets the rows below for it.";
  row.value = [draft] { return TypeLabel(draft->type); };
  row.adjust = [draft](int direction) { draft->ChangeType(direction); };
  row.unapplied = [draft] { return draft->type != WheelTypeOf(draft->config); };
  return row;
}

MenuRow ValueRow(const std::shared_ptr<Draft>& draft, const WheelRow& wheel_row) {
  const std::string_view key = wheel_row.key;
  MenuRow row;
  row.label = std::string(wheel_row.label);
  row.description = std::string(wheel_row.description);
  row.divider_above = key == "wheel_ffb_constant";
  row.value = [draft, key] {
    const int value = draft->values.find(key)->second;
    return std::to_string(value) + (key == kSteeringLockKey ? " DEGREES" : " %");
  };
  row.adjust = [draft, key, step = wheel_row.step, min = wheel_row.min,
                max = wheel_row.max](int direction) {
    const int rotation = WheelRotation(draft->config);
    const int low = key == kSteeringLockKey ? MinSteeringLock(rotation) : min;
    const int high = key == kSteeringLockKey ? rotation : max;
    draft->Change(key, std::clamp(draft->values.find(key)->second + direction * step, low, high));
  };
  row.badge = [key] { return LaunchOptionBadge(key); };
  row.unapplied = [draft, key] { return draft->Unapplied(key); };
  return row;
}

}  // namespace

std::unique_ptr<MenuScreen> CreateWheelPage(config::HostConfig& config) {
  auto draft = std::make_shared<Draft>(config);
  std::vector<MenuRow> rows;
  rows.push_back(TypeRow(draft));
  for (const WheelRow& row : WheelRows()) {
    rows.push_back(ValueRow(draft, row));
  }
  auto screen = std::make_unique<MenuScreen>("WHEEL", std::move(rows));
  screen->set_apply([draft] { draft->SaveAndApply(); });
  screen->set_leave([draft] {
    if (!draft->Changed() || draft->leave_warned) {
      return true;
    }
    draft->leave_warned = true;
    return false;
  });
  screen->set_status([draft] {
    if (draft->save_failed) {
      return hostui::MenuStatus{.text = "COULD NOT SAVE WHEEL.TOML", .warning = true};
    }
    if (draft->replace_warned) {
      return hostui::MenuStatus{.text = "THIS REPLACES YOUR CUSTOM VALUES: PRESS Y AGAIN",
                                .warning = true};
    }
    if (draft->Changed()) {
      return hostui::MenuStatus{.text = "UNSAVED CHANGES", .warning = draft->leave_warned};
    }
    if (draft->applied) {
      return hostui::MenuStatus{.text = "SETTINGS SAVED AND APPLIED."};
    }
    return hostui::MenuStatus{};
  });
  screen->set_update([draft](bool closing) {
    if (closing && draft->Changed()) {
      REXLOG_INFO("Wheel page: discarded unapplied changes");
    }
  });
  return screen;
}

}  // namespace fm4_static_recomp::ui
