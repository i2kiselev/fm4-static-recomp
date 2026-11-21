// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "input/wheel_taps.h"

#include <cstdlib>

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(fm4_menu_mouse, true, "Forza Motorsport 4",
                    "The mouse in menus: left click selects, right click goes back, the wheel moves "
                    "through lists; false leaves the mouse to the race alone");

namespace fm4_static_recomp::input {
namespace {

// X_INPUT_GAMEPAD_DPAD_* (kept here so the class tests without the SDK).
constexpr uint16_t kDpadUp = 0x0001, kDpadDown = 0x0002, kDpadLeft = 0x0004, kDpadRight = 0x0008;
constexpr uint16_t kA = 0x1000, kB = 0x2000;
constexpr uint16_t kDpad = kDpadUp | kDpadDown | kDpadLeft | kDpadRight;

}  // namespace

void WheelTaps::AddNotches(int notches, bool sideways) {
  if (notches == 0) return;
  const uint16_t button = sideways ? (notches > 0 ? kDpadLeft : kDpadRight) : (notches > 0 ? kDpadUp : kDpadDown);
  std::lock_guard lock(mutex_);
  // A turn the other way drops what is still queued the first way.
  if (!queued_.empty() && (queued_.back().buttons & kDpad) && queued_.back().buttons != button) queued_.clear();
  for (int i = 0; i < std::abs(notches) && queued_.size() < kMaxQueued; ++i) queued_.push_back({button, false});
}

void WheelTaps::AddClick(bool right) {
  std::lock_guard lock(mutex_);
  if (queued_.size() < kMaxQueued + kMaxSteps) queued_.push_back({right ? kB : kA, false});
}

void WheelTaps::AddPress(uint32_t buttons) {
  std::lock_guard lock(mutex_);
  if (buttons != 0 && queued_.size() < kMaxQueued + kMaxSteps) queued_.push_back({buttons, false});
}

void WheelTaps::AddSteps(uint16_t button, int count) {
  std::lock_guard lock(mutex_);
  for (int i = 0; i < count && i < kMaxSteps; ++i) queued_.push_back({button, true});
}

void WheelTaps::Hold(uint32_t buttons) {
  std::lock_guard lock(mutex_);
  held_ = buttons;
}

bool WheelTaps::Busy() {
  std::lock_guard lock(mutex_);
  return current_.buttons != 0 || !queued_.empty();
}

uint32_t WheelTaps::Buttons(double elapsed_seconds) {
  std::lock_guard lock(mutex_);
  const auto hold = [this] { return current_.quick ? kStepSeconds : kHoldSeconds; };
  const auto gap = [this] { return current_.quick ? kStepSeconds : kGapSeconds; };
  if (current_.buttons != 0) {
    phase_ += elapsed_seconds;
    if (phase_ >= hold() + gap()) {
      current_ = {};
    }
  }
  if (current_.buttons == 0 && !queued_.empty()) {
    current_ = queued_.front();
    queued_.pop_front();
    phase_ = 0;
  }
  return (current_.buttons != 0 && phase_ < hold() ? current_.buttons : 0) | held_;
}

void WheelTaps::Clear() {
  std::lock_guard lock(mutex_);
  queued_.clear();
  current_ = {};
  phase_ = 0;
  held_ = 0;
}

namespace {
std::atomic<bool> g_mouse_last{false};
}  // namespace

bool MenuMouseEnabled() { return REXCVAR_GET(fm4_menu_mouse); }
void SetMouseLast(bool mouse) { g_mouse_last = mouse; }
bool MouseLast() { return g_mouse_last && MenuMouseEnabled(); }

WheelTaps& MenuWheelTaps() {
  static WheelTaps taps;
  return taps;
}

}  // namespace fm4_static_recomp::input
