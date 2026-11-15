// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <rex/ui/virtual_key.h>

namespace fm4_static_recomp::hostui {

enum class NavCommand {
  kUp,
  kDown,
  kLeft,
  kRight,
  kAccept,
  kBack,
  kClose,  // leave every screen (Start, like the title's pause menu)
  kApply,  // apply the screen's drafted changes (Y)
  kClear,  // clear the focused key slot (X)
  kPreviousTab,  // LB
  kNextTab,      // RB
};

// The keyboard's keys for the menu's pad buttons, in the keyboard driver's bind syntax
// ("Up,W", "Y").
struct MenuKeys {
  std::string up;
  std::string down;
  std::string left;
  std::string right;
  std::string accept;
  std::string back;
  std::string apply;
  std::string clear;
  std::string previous_tab;
  std::string next_tab;
};

// The command a key press gives: the arrows, Numpad 8 4 2 6, Enter, Space, Esc and
// Backspace always (so no rebinding locks the page), then the menu binds. Directions repeat
// with the key; the others act once per press.
std::optional<NavCommand> KeyCommand(const MenuKeys& keys, rex::ui::VirtualKey key, bool repeat);

// A whole number in an inclusive range, drawn as the game's tuning slider.
struct MenuSlider {
  // Drawn at the ends of the slider section's header.
  std::function<std::pair<int, int>()> range;
  std::function<int()> get;
  std::function<void(int value)> set;
};

// One row of a host menu: a label and a value beside it, changed with
// `adjust` (-1 or +1), or a slider stepped by one. The focused row's
// description, note and badge show in the description panel.
struct MenuRow {
  std::string label;
  std::function<std::string()> value;
  std::function<void(int direction)> adjust;
  std::optional<MenuSlider> slider;
  std::function<bool()> enabled;
  std::string description;
  // Current state worth knowing, or empty.
  std::function<std::string()> note;
  // Where the value comes from when not the page, or empty.
  std::function<std::string()> badge;
  // A drafted change, not applied yet.
  std::function<bool()> unapplied;
  // A thin line above the row: it starts a block of rows.
  bool divider_above = false;
  // A section's title in the list, never focused.
  bool heading = false;
  // A row of key slots instead of a value: the slots' texts, drawn in the screen's columns.
  // Left and right move between the slots, accept picks the focused one and clear (X)
  // clears it.
  std::function<std::vector<std::string>()> slots;
  std::function<void(size_t slot)> pick;
  std::function<void(size_t slot)> clear;
  // What clear does to the slot, for the button help; empty when it does nothing there.
  std::function<std::string(size_t slot)> clear_label;

  bool is_enabled() const { return !heading && (!enabled || enabled()); }
};

// A screen waiting for a key press after a slot was picked: the host hands it the next
// key, or cancels.
struct MenuKeyCapture {
  std::function<bool()> active;
  // The key in the keyboard driver's bind syntax ("W", "Return").
  std::function<void(const std::string& key)> key;
  std::function<void()> cancel;
};

// A screen's own state, shown beside the button help.
struct MenuStatus {
  std::string text;
  // Applying: the spinner turns, and the rows and leaving wait.
  bool busy = false;
  // Leaving now discards changes: drawn red, with BACK as DISCARD.
  bool warning = false;
};

class MenuScreen {
 public:
  MenuScreen(std::string title, std::vector<MenuRow> rows);

  const std::string& title() const { return title_; }
  const std::vector<MenuRow>& rows() const { return rows_; }
  // Called on every drawn frame, and with closing set when the screen leaves.
  void set_update(std::function<void(bool closing)> update) { update_ = std::move(update); }
  void Update(bool closing) {
    if (update_) {
      update_(closing);
    }
  }
  // A screen whose rows only draft their changes: apply (Y) applies them.
  void set_apply(std::function<void()> apply) { apply_ = std::move(apply); }
  bool has_apply() const { return bool(apply_); }
  // Asked as back or close would leave the screen; false keeps it open.
  void set_leave(std::function<bool()> leave) { leave_ = std::move(leave); }
  void set_status(std::function<MenuStatus()> status) { status_ = std::move(status); }
  MenuStatus status() const { return status_ ? status_() : MenuStatus{}; }
  size_t focus() const { return focus_; }
  // Moves the focus to `index` if that row is enabled.
  bool SetFocus(size_t index);
  // The focused slot of a row of slots; the same column on every row.
  size_t slot() const { return slot_; }
  void SetSlot(size_t slot) { slot_ = slot; }
  // Column headers: the first over the labels, then one over each column of values.
  // Empty: CATEGORY and ADJUSTABLE OPTION.
  void set_headers(std::vector<std::string> headers) { headers_ = std::move(headers); }
  const std::vector<std::string>& headers() const { return headers_; }
  void set_capture(MenuKeyCapture capture) { capture_ = std::move(capture); }
  bool capturing() const { return capture_.active && capture_.active(); }
  // Whether the screen may be left now, as back would ask it.
  bool MayLeave() { return !leave_ || leave_(); }

  enum class Result { kNone, kChanged, kBack };
  // Hands a capturing screen its key.
  Result CaptureKey(const std::string& key);
  // Applies a navigation command: up and down move the focus over enabled
  // rows with wrap-around, left and right adjust the focused row (or move
  // between its slots), accept steps it forward (or picks the slot), back
  // leaves the screen unless the screen refuses. Only the focus moves while
  // the screen is busy. While capturing, back cancels and nothing else acts.
  Result Handle(NavCommand command);
  // Moves the focused slider by `steps`, within its range.
  Result Slide(int steps);
  // Sets the focused slider to the value at `fraction` (0..1) of its range.
  Result SlideTo(float fraction);

 private:
  std::string title_;
  std::vector<MenuRow> rows_;
  std::function<void(bool closing)> update_;
  std::function<void()> apply_;
  std::function<bool()> leave_;
  std::function<MenuStatus()> status_;
  MenuKeyCapture capture_;
  std::vector<std::string> headers_;
  size_t focus_ = 0;
  size_t slot_ = 0;
};

// Turns polled pad state into navigation commands with the usual menu key
// repeat. Buttons use the XINPUT_GAMEPAD bit values.
class PadNavigator {
 public:
  static constexpr uint16_t kDpadUp = 0x0001;
  static constexpr uint16_t kDpadDown = 0x0002;
  static constexpr uint16_t kDpadLeft = 0x0004;
  static constexpr uint16_t kDpadRight = 0x0008;
  static constexpr uint16_t kStart = 0x0010;
  static constexpr uint16_t kBack = 0x0020;
  static constexpr uint16_t kLeftShoulder = 0x0100;
  static constexpr uint16_t kRightShoulder = 0x0200;
  static constexpr uint16_t kButtonA = 0x1000;
  static constexpr uint16_t kButtonB = 0x2000;
  static constexpr uint16_t kButtonX = 0x4000;
  static constexpr uint16_t kButtonY = 0x8000;
  static constexpr int16_t kStickThreshold = 16000;
  static constexpr uint64_t kRepeatDelayMs = 400;
  static constexpr uint64_t kRepeatIntervalMs = 110;

  // Returns the commands for this poll. Directions (d-pad or left stick)
  // repeat while held; A, B, X, Y, LB, RB and Start fire once per press, Start not while
  // Back is held (BACK+START is the app's). The first poll after
  // Reset only records what is held, so the press that opened the UI does
  // not also act inside it. With `stick_x_free` the stick's left and right
  // are left to the caller.
  std::vector<NavCommand> Poll(uint16_t buttons, int16_t thumb_lx, int16_t thumb_ly,
                               uint64_t now_ms, bool stick_x_free = false);
  void Reset() { primed_ = false; }

 private:
  bool primed_ = false;
  uint16_t previous_buttons_ = 0;
  int held_direction_ = -1;
  uint64_t next_repeat_ms_ = 0;
};

// The first row a list of `visible_rows` shows from `list_begin` on. Keys and the wheel keep
// the focus in the middle; `kept_first`, the list as the pointer found it, stays put while the
// focus is on it, so pointing at the bottom row does not scroll the list.
size_t FirstVisibleRow(size_t row_count, size_t list_begin, size_t visible_rows, size_t focus,
                       std::optional<size_t> kept_first);

}  // namespace fm4_static_recomp::hostui
