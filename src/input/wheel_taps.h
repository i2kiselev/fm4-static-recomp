// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>

namespace fm4_static_recomp::input {

// The mouse in menus: a left click is an A tap, a right click a B tap, and each wheel notch one
// D-pad tap, up/down, or left/right with Shift held (the car and track scrollers). Each tap is held
// kHoldSeconds, then released kGapSeconds before the next, so the game sees every one. Notches queue up
// to kMaxQueued so a fast spin does not run on after it stops. Hover steps (the focus following the
// pointer) are quicker: kStepSeconds held, kStepSeconds released.
class WheelTaps {
 public:
  static constexpr double kHoldSeconds = 0.06;
  static constexpr double kGapSeconds = 0.04;
  static constexpr size_t kMaxQueued = 8;
  static constexpr double kStepSeconds = 0.034;  // one UI frame down, one up: the shortest the game sees
  static constexpr int kMaxSteps = 64;

  // From the window's wheel events: notches > 0 is away from the player (up, or left with Shift).
  void AddNotches(int notches, bool sideways);
  // From the keyboard filter, once per poll: the buttons to press now (X_INPUT_GAMEPAD_* bits, and
  // kLeftTriggerBit / kRightTriggerBit in menu_hit.h for the triggers).
  uint32_t Buttons(double elapsed_seconds);
  // From the window's clicks: left is A (select), right is B (back).
  void AddClick(bool right);
  // A clicked prompt: one press of its buttons, after what is queued.
  void AddPress(uint32_t buttons);
  // Hover: `count` quick presses of one D-pad button, after what is queued.
  void AddSteps(uint16_t button, int count);
  // Presses are queued or being played.
  bool Busy();
  // Buttons held down until changed (a slider drag holds left or right: the game's own repeat).
  void Hold(uint32_t buttons);
  // Leaving the menus drops what is queued.
  void Clear();

 private:
  std::mutex mutex_;
  struct Tap {
    uint32_t buttons = 0;
    bool quick = false;
  };
  std::deque<Tap> queued_;
  Tap current_;           // the tap being played
  uint32_t held_ = 0;
  double phase_ = 0;      // seconds into the current tap (hold, then gap)
};

WheelTaps& MenuWheelTaps();

// What the window tells the mouse in menus besides the mouse itself: when it got focus (the click that
// brings it to the front is not a menu click) and whether Shift is held (the wheel then moves sideways).
class MenuMouseKeys {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::chrono::milliseconds kFocusClickWindow{300};

  void Focused(Clock::time_point now) { focused_at_ = now; }
  bool ClickCounts(Clock::time_point now) const { return now - focused_at_ >= kFocusClickWindow; }
  // A key going down or up; true when it was another key than Shift (it gives the prompts back to the keys).
  bool Key(bool shift_key, bool down) {
    if (shift_key) {
      shift_ = down;
      return false;
    }
    return down;
  }
  bool shift() const { return shift_; }

 private:
  Clock::time_point focused_at_{};
  bool shift_ = false;
};

// The mouse in menus is on unless the session is launched with --fm4_menu_mouse=false.
bool MenuMouseEnabled();

// The mouse was the last thing used in a menu (the prompts show its buttons); a key or the pad
// clears it.
void SetMouseLast(bool mouse);
bool MouseLast();

}  // namespace fm4_static_recomp::input
