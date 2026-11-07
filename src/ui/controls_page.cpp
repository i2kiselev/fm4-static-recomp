// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "ui/controls_page.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/logging.h>

#include "input/controller_layout.h"
#include "input/controls.h"
#include "input/keycap.h"

namespace fm4_static_recomp::ui {
namespace {

using hostui::MenuRow;
using hostui::MenuScreen;
using input::PadControl;

constexpr double kRampLow = 0.02;
constexpr double kRampHigh = 0.60;

// The launcher's CONTROLS order (ControlsCatalog.cs): the controls each layout uses first.
constexpr PadControl kDrivingOrder[] = {
    PadControl::kRightTrigger, PadControl::kLeftTrigger,  PadControl::kLStickLeft,
    PadControl::kLStickRight,  PadControl::kA,            PadControl::kB,
    PadControl::kX,            PadControl::kLeftShoulder, PadControl::kY,
    PadControl::kBack,         PadControl::kRightShoulder, PadControl::kRStickUp,
    PadControl::kRStickDown,   PadControl::kRStickLeft,   PadControl::kRStickRight,
    PadControl::kRStickPress,  PadControl::kDpadUp,       PadControl::kDpadDown,
    PadControl::kStart,        PadControl::kLStickUp,     PadControl::kLStickDown,
    PadControl::kLStickPress,  PadControl::kDpadLeft,     PadControl::kDpadRight};
constexpr PadControl kMenuOrder[] = {
    PadControl::kA,           PadControl::kB,            PadControl::kX,
    PadControl::kY,           PadControl::kLeftShoulder, PadControl::kRightShoulder,
    PadControl::kLeftTrigger, PadControl::kRightTrigger, PadControl::kDpadUp,
    PadControl::kDpadDown,    PadControl::kDpadLeft,     PadControl::kDpadRight,
    PadControl::kBack,        PadControl::kStart,        PadControl::kLStickUp,
    PadControl::kLStickDown,  PadControl::kLStickLeft,   PadControl::kLStickRight,
    PadControl::kLStickPress, PadControl::kRStickUp,     PadControl::kRStickDown,
    PadControl::kRStickLeft,  PadControl::kRStickRight,  PadControl::kRStickPress};

constexpr const char* kPadNames[] = {
    "A",           "B",           "X",           "Y",            "LB",
    "RB",          "LT",          "RT",          "L-STICK UP",   "L-STICK DOWN",
    "L-STICK LEFT", "L-STICK RIGHT", "L-STICK PRESS", "R-STICK UP", "R-STICK DOWN",
    "R-STICK LEFT", "R-STICK RIGHT", "R-STICK PRESS", "D-PAD UP",   "D-PAD DOWN",
    "D-PAD LEFT",  "D-PAD RIGHT", "BACK",        "START"};
static_assert(std::size(kPadNames) == size_t(PadControl::kCount));

struct Ramp {
  const char* name;
  const char* label;
  double input::RampRates::*rate;
};

constexpr Ramp kRamps[] = {
    {"ramp_steer_seconds", "STEERING: REST TO FULL", &input::RampRates::steer_seconds},
    {"ramp_steer_return_seconds", "STEERING: BACK TO REST",
     &input::RampRates::steer_return_seconds},
    {"ramp_pedal_seconds", "PEDALS: REST TO FULL", &input::RampRates::pedal_seconds},
    {"ramp_pedal_return_seconds", "PEDALS: BACK TO REST",
     &input::RampRates::pedal_return_seconds},
    {"ramp_look_seconds", "LOOKING AROUND", &input::RampRates::look_seconds},
};

std::string Upper(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    c = char(std::toupper(uint8_t(c)));
  }
  return out;
}

std::vector<std::string> Split(std::string_view value) {
  std::vector<std::string> out;
  while (!value.empty()) {
    const size_t comma = value.find(',');
    std::string_view token = value.substr(0, comma);
    value = comma == std::string_view::npos ? std::string_view() : value.substr(comma + 1);
    while (token.starts_with(' '))
      token.remove_prefix(1);
    while (token.ends_with(' '))
      token.remove_suffix(1);
    if (!token.empty()) {
      out.emplace_back(token);
    }
  }
  return out;
}

std::string Join(const std::vector<std::string>& keys) {
  std::string out;
  for (const std::string& key : keys) {
    out += (out.empty() ? "" : ",") + key;
  }
  return out;
}

// The key once modifiers are left out, as the game compares keys (BareKey in controls.cpp).
std::string_view BareKey(std::string_view token) {
  const size_t plus = token.rfind('+');
  return plus == std::string_view::npos || plus + 1 == token.size() ? token
                                                                     : token.substr(plus + 1);
}

size_t IndexOf(const std::vector<std::string>& keys, std::string_view key) {
  for (size_t i = 0; i < keys.size(); ++i) {
    if (BareKey(keys[i]) == BareKey(key)) {
      return i;
    }
  }
  return keys.size();
}

// The fonts have no arrows: the arrow keys in words.
std::string KeyText(std::string_view token) {
  if (token == "Up" || token == "Down" || token == "Left" || token == "Right") {
    return Upper(token);
  }
  return input::KeyLabel(token);
}

std::string KeysText(const std::vector<std::string>& keys) {
  std::string out;
  for (const std::string& key : keys) {
    out += (out.empty() ? "" : ", ") + KeyText(key);
  }
  return out;
}

// As the launcher writes ramp times (0.##).
std::string FormatRamp(double seconds) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.2f", seconds);
  std::string out = text;
  while (out.ends_with('0')) {
    out.pop_back();
  }
  if (out.ends_with('.')) {
    out.pop_back();
  }
  return out;
}

enum class KeySet { kDriving, kMenu };

// One row of keys: a pad control in one of the two sets.
struct KeyRowId {
  KeySet set;
  PadControl control;
  bool operator==(const KeyRowId&) const = default;
};

using RowLabels = std::vector<std::pair<KeyRowId, std::string>>;

struct Edit {
  std::string value;
  std::string default_value;
  bool quoted;
};

struct Capture {
  KeyRowId row;
  size_t slot;
  std::chrono::steady_clock::time_point started;
};

struct Draft {
  Draft(config::HostConfig& config, ControlsPageServices services)
      : config(config), services(std::move(services)) {}

  config::HostConfig& config;
  ControlsPageServices services;
  // The edited lines whose values differ from the file's.
  std::map<std::string, Edit> edits;
  std::optional<Capture> capture;
  bool leave_warned = false;
  bool applied = false;
  bool save_failed = false;
  bool apply_failed = false;
  // About the latest key press, until the next change.
  std::string message;

  static const input::BindingSet& Set(const input::Controls& controls, KeySet set) {
    return set == KeySet::kDriving ? controls.driving : controls.menu;
  }

  static std::string FileName(KeyRowId row) {
    return std::string(row.set == KeySet::kDriving ? "driving_" : "menu_") +
           std::string(input::ControlFileName(row.control));
  }

  static std::string DefaultKeys(KeyRowId row) {
    static const input::Controls defaults = input::DefaultControls();
    return Set(defaults, row.set).keys[size_t(row.control)];
  }

  // The file's value as the game reads it: keys it knows, a ramp it can parse; normalized as
  // the page writes them.
  std::optional<std::string> Saved(std::string_view name) const {
    const auto value = config.Get(name);
    if (!value) {
      return std::nullopt;
    }
    if (name.starts_with("ramp_")) {
      const auto seconds = input::ParseRampSeconds(*value);
      return seconds ? std::optional(FormatRamp(*seconds)) : std::nullopt;
    }
    std::string keys = input::UsableKeys(*value);
    return keys.empty() ? std::nullopt : std::optional(std::move(keys));
  }

  // The value the game will read for `name` once the edits are saved.
  std::optional<std::string> Lookup(std::string_view name) const {
    if (const auto it = edits.find(std::string(name)); it != edits.end()) {
      return it->second.value == it->second.default_value ? std::nullopt
                                                          : std::optional(it->second.value);
    }
    return Saved(name);
  }

  const input::Controls& Effective() const {
    if (!effective) {
      effective = input::LoadControls([this](std::string_view name) { return Lookup(name); },
                                      false);
    }
    return *effective;
  }

  // A line the game takes as the player's own: the default controls give up its keys.
  bool Explicit(KeyRowId row) const { return Lookup(FileName(row)).has_value(); }

  // The keys the row shows and edits: the player's own, or what the game makes of the default.
  std::vector<std::string> Shown(KeyRowId row) const {
    if (const auto value = Lookup(FileName(row))) {
      return Split(*value);
    }
    return Split(Set(Effective(), row.set).keys[size_t(row.control)]);
  }

  void Store(const std::string& name, std::string value, std::string default_value,
             bool quoted) {
    const auto saved = Saved(name);
    if (saved ? value == *saved : value == default_value) {
      edits.erase(name);
    } else {
      edits[name] = Edit{std::move(value), std::move(default_value), quoted};
    }
    Changed();
  }

  void StoreKeys(KeyRowId row, const std::vector<std::string>& keys) {
    Store(FileName(row), Join(keys), DefaultKeys(row), true);
  }

  void Changed() {
    effective.reset();
    leave_warned = false;
    applied = false;
    save_failed = false;
    apply_failed = false;
    message.clear();
  }

  void TakeKey(const std::string& key) {
    const Capture target = *capture;
    capture.reset();
    std::vector<std::string> keys = Shown(target.row);
    const size_t other = IndexOf(keys, key);
    if (other < keys.size()) {
      if (other == target.slot) {
        return;
      }
      if (target.slot >= keys.size()) {
        message = KeyText(key) + " IS ALREADY ON THIS BUTTON.";
        return;
      }
      std::swap(keys[other], keys[target.slot]);
    } else if (target.slot < keys.size()) {
      keys[target.slot] = key;
    } else {
      keys.push_back(key);
    }
    StoreKeys(target.row, keys);
  }

  void SaveAndApply() {
    if (edits.empty() || capture) {
      return;
    }
    const bool loaded = config.Load();
    if (loaded) {
      for (const auto& [name, edit] : edits) {
        if (edit.value == edit.default_value) {
          config.Remove(name);
        } else {
          config.Set(name, edit.quoted ? config::Quote(edit.value) : edit.value);
        }
      }
    }
    if (!loaded || !config.Save()) {
      REXLOG_ERROR("Controls page: could not write {}", config.path().string());
      // The edits stay drafted; the shared file text goes back to what is on disk.
      config.Load();
      effective.reset();
      save_failed = true;
      return;
    }
    REXLOG_INFO("Controls page: saved {} changes to {}", edits.size(), config.path().string());
    edits.clear();
    effective.reset();
    apply_failed = services.apply && !services.apply();
    applied = !apply_failed;
    leave_warned = false;
    message.clear();
  }

 private:
  mutable std::optional<input::Controls> effective;
};

std::string RowLabel(KeyRowId row, uint32_t layout) {
  const std::string pad = kPadNames[size_t(row.control)];
  std::string_view action;
  if (row.set == KeySet::kDriving) {
    action = input::DrivingAction(layout, row.control);
  } else {
    switch (row.control) {
      case PadControl::kA:
        action = "Select";
        break;
      case PadControl::kB:
        action = "Back";
        break;
      case PadControl::kDpadUp:
        action = "Move up";
        break;
      case PadControl::kDpadDown:
        action = "Move down";
        break;
      case PadControl::kDpadLeft:
        action = "Move left";
        break;
      case PadControl::kDpadRight:
        action = "Move right";
        break;
      default:
        break;
    }
  }
  if (action.empty()) {
    return row.control == PadControl::kBack || row.control == PadControl::kStart
               ? pad + " BUTTON"
               : pad;
  }
  return Upper(action) + " (" + pad + ")";
}

std::string RowDescription(KeyRowId row, uint32_t layout) {
  const std::string pad = kPadNames[size_t(row.control)];
  const std::string how =
      " LEFT and RIGHT pick the primary or secondary key; CHANGE waits for the new key.";
  if (row.set == KeySet::kMenu) {
    return "Keys that press " + pad +
           " on the pad in menus, the pre-race screen, the pause menu, replays and results." +
           how;
  }
  const std::string_view action = input::DrivingAction(layout, row.control);
  return "Keys that press " + pad + " on the pad while the car is on the track. " +
         (action.empty() ? "The controller layout chosen in the game's options does not use it."
                         : "In the controller layout chosen in the game's options it is " +
                               std::string(action) + ".") +
         how;
}

// Why the row's keys are not what it asks for: a default whose keys another row of its set
// took, or a key the player gave to two rows of a set.
std::string RowNote(const Draft& draft, KeyRowId row, const RowLabels& labels) {
  const std::vector<std::string> keys = draft.Shown(row);
  const auto others = [&](auto&& visit) {
    for (const auto& [other, label] : labels) {
      if (other.set == row.set && other != row && draft.Explicit(other)) {
        visit(draft.Shown(other), label);
      }
    }
  };
  if (!draft.Explicit(row)) {
    std::vector<std::string> lost;
    std::vector<std::string> takers;
    for (const std::string& key : Split(draft.DefaultKeys(row))) {
      if (IndexOf(keys, key) < keys.size()) {
        continue;
      }
      lost.push_back(key);
      others([&](const std::vector<std::string>& theirs, const std::string& label) {
        if (IndexOf(theirs, key) < theirs.size() &&
            std::find(takers.begin(), takers.end(), label) == takers.end()) {
          takers.push_back(label);
        }
      });
    }
    if (lost.empty()) {
      return {};
    }
    std::string by;
    for (const std::string& taker : takers) {
      by += (by.empty() ? "" : ", ") + taker;
    }
    return KeysText(lost) + (lost.size() > 1 ? " are" : " is") + " on " +
           (by.empty() ? std::string("another button") : by) +
           " now, so the game gives this button " + KeysText(keys) + ".";
  }
  std::string shared;
  others([&](const std::vector<std::string>& theirs, const std::string& label) {
    if (std::ranges::any_of(theirs, [&](const std::string& key) {
          return IndexOf(keys, key) < keys.size();
        })) {
      shared += (shared.empty() ? "" : ", ") + label;
    }
  });
  return shared.empty() ? std::string()
                        : "Also used by " + shared + ": one press of the key presses both.";
}

MenuRow KeyRow(const std::shared_ptr<Draft>& draft, KeyRowId id, std::string label,
               uint32_t layout, std::shared_ptr<const RowLabels> labels) {
  MenuRow row;
  row.label = std::move(label);
  row.description = RowDescription(id, layout);
  row.slots = [draft, id] {
    const std::vector<std::string> keys = draft->Shown(id);
    std::vector<std::string> slots = {
        keys.empty() ? std::string("NOT SET") : KeyText(keys[0]),
        keys.size() > 1 ? KeyText(keys[1]) + (keys.size() > 2
                                                  ? " +" + std::to_string(keys.size() - 2)
                                                  : std::string())
                        : std::string("NOT SET")};
    if (draft->capture && draft->capture->row == id) {
      slots[std::min<size_t>(draft->capture->slot, 1)] = "PRESS A KEY";
    }
    return slots;
  };
  row.pick = [draft, id](size_t slot) {
    draft->message.clear();
    draft->capture = Capture{id, slot, draft->services.now()};
  };
  row.clear_label = [draft, id](size_t slot) {
    if (slot == 0) {
      return draft->Explicit(id) ? std::string("DEFAULT KEYS") : std::string();
    }
    return slot < draft->Shown(id).size() ? std::string("CLEAR KEY") : std::string();
  };
  row.clear = [draft, id](size_t slot) {
    if (slot == 0) {
      draft->StoreKeys(id, Split(Draft::DefaultKeys(id)));
      return;
    }
    std::vector<std::string> keys = draft->Shown(id);
    keys.erase(keys.begin() + std::ptrdiff_t(slot));
    draft->StoreKeys(id, keys);
  };
  row.note = [draft, id, labels] { return RowNote(*draft, id, *labels); };
  row.unapplied = [draft, id] { return draft->edits.contains(Draft::FileName(id)); };
  return row;
}

MenuRow RampRow(const std::shared_ptr<Draft>& draft, const Ramp& ramp) {
  MenuRow row;
  row.label = ramp.label;
  row.description =
      "Seconds a key takes to move steering, the pedals or looking around to full, and back "
      "to rest when released. Lower is snappier.";
  row.value = [draft, &ramp] {
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f S", draft->Effective().ramp.*ramp.rate);
    return std::string(text);
  };
  row.adjust = [draft, &ramp](int direction) {
    const double current = draft->Effective().ramp.*ramp.rate;
    const double next =
        std::clamp(std::round(current * 100.0 + direction) / 100.0, kRampLow, kRampHigh);
    draft->Store(ramp.name, FormatRamp(next), FormatRamp(input::DefaultControls().ramp.*ramp.rate),
                 false);
  };
  row.unapplied = [draft, &ramp] { return draft->edits.contains(ramp.name); };
  return row;
}

MenuRow Heading(const char* label) {
  MenuRow row;
  row.label = label;
  row.heading = true;
  row.divider_above = true;
  return row;
}

}  // namespace

std::unique_ptr<MenuScreen> CreateControlsPage(config::HostConfig& config,
                                               ControlsPageServices services) {
  const uint32_t layout = services.controller_layout ? services.controller_layout() : 0;
  auto draft = std::make_shared<Draft>(config, std::move(services));

  auto labels = std::make_shared<RowLabels>();
  for (const PadControl control : kDrivingOrder) {
    labels->emplace_back(KeyRowId{KeySet::kDriving, control},
                         RowLabel({KeySet::kDriving, control}, layout));
  }
  for (const PadControl control : kMenuOrder) {
    labels->emplace_back(KeyRowId{KeySet::kMenu, control},
                         RowLabel({KeySet::kMenu, control}, layout));
  }
  std::vector<MenuRow> rows;
  const auto add_keys = [&](KeySet set) {
    for (const auto& [id, label] : *labels) {
      if (id.set == set) {
        rows.push_back(KeyRow(draft, id, label, layout, labels));
      }
    }
  };
  rows.push_back(Heading("DRIVING"));
  add_keys(KeySet::kDriving);
  for (const Ramp& ramp : kRamps) {
    rows.push_back(RampRow(draft, ramp));
  }
  rows.push_back(Heading("MENUS"));
  add_keys(KeySet::kMenu);
  MenuRow reset;
  reset.label = "DEFAULTS";
  reset.divider_above = true;
  reset.description =
      "Sets every key and the ramp times back to the game's defaults. SAVE & APPLY writes them.";
  reset.slots = [] { return std::vector<std::string>{"RESET ALL"}; };
  reset.pick = [draft, labels](size_t) {
    for (const auto& [id, label] : *labels) {
      draft->StoreKeys(id, Split(Draft::DefaultKeys(id)));
    }
    for (const Ramp& ramp : kRamps) {
      const std::string value = FormatRamp(input::DefaultControls().ramp.*ramp.rate);
      draft->Store(ramp.name, value, value, false);
    }
  };
  rows.push_back(std::move(reset));

  auto screen = std::make_unique<MenuScreen>("KEYBOARD", std::move(rows));
  screen->set_headers({"ACTION", "PRIMARY", "SECONDARY"});
  screen->set_capture(hostui::MenuKeyCapture{
      .active = [draft] { return draft->capture.has_value(); },
      .key = [draft](const std::string& key) { draft->TakeKey(key); },
      .cancel = [draft] { draft->capture.reset(); }});
  screen->set_apply([draft] { draft->SaveAndApply(); });
  screen->set_leave([draft] {
    if (draft->edits.empty() || draft->leave_warned) {
      return true;
    }
    draft->leave_warned = true;
    return false;
  });
  screen->set_status([draft, labels] {
    if (draft->capture) {
      const auto left = kCaptureTimeout - (draft->services.now() - draft->capture->started);
      const auto seconds = std::max<long long>(
          1, (std::chrono::duration_cast<std::chrono::milliseconds>(left).count() + 999) / 1000);
      std::string label;
      for (const auto& [id, text] : *labels) {
        if (id == draft->capture->row) {
          label = text;
        }
      }
      return hostui::MenuStatus{.text = "PRESS THE KEY FOR " + label + " (CANCELS IN " +
                                        std::to_string(seconds) + " S)"};
    }
    if (draft->save_failed) {
      return hostui::MenuStatus{.text = "COULD NOT SAVE CONTROLS.TOML", .warning = true};
    }
    if (draft->apply_failed) {
      return hostui::MenuStatus{.text = "KEYS SAVED; THEY APPLY AT THE NEXT START.",
                                .warning = true};
    }
    if (!draft->edits.empty()) {
      return hostui::MenuStatus{.text = "UNSAVED CHANGES", .warning = draft->leave_warned};
    }
    if (!draft->message.empty()) {
      return hostui::MenuStatus{.text = draft->message};
    }
    if (draft->applied) {
      return hostui::MenuStatus{.text = "KEYS SAVED AND APPLIED."};
    }
    return hostui::MenuStatus{};
  });
  screen->set_update([draft](bool closing) {
    if (draft->capture &&
        (closing || draft->services.now() - draft->capture->started >= kCaptureTimeout)) {
      draft->capture.reset();
    }
    if (closing && !draft->edits.empty()) {
      REXLOG_INFO("Controls page: discarded {} unapplied changes", draft->edits.size());
    }
  });
  return screen;
}

}  // namespace fm4_static_recomp::ui
