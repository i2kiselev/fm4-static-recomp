// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "quick_restart.h"

#include <array>
#include <cmath>
#include <utility>

#include <rex/cvar.h>

REXCVAR_DEFINE_STRING(fm4_quick_restart, "off", "Forza Motorsport 4",
                      "QUICK RESTART while driving: off, dpad_left or dpad_right (the D-pad sides no "
                      "controller layout uses while driving)");

REXCVAR_DEFINE_STRING(fm4_quick_restart_trigger, "hold", "Forza Motorsport 4",
                      "QUICK RESTART: hold (hold the button through the fade; let go to cancel) or tap "
                      "(one press restarts)");

namespace fm4_static_recomp {
namespace {

// X_INPUT_GAMEPAD button bits.
constexpr uint16_t kLeft = 0x0004, kRight = 0x0008, kStart = 0x0010, kA = 0x1000, kB = 0x2000;

constexpr double kPress = 0.15;        // how long each press is held
constexpr double kPauseTimeout = 2.0;  // START pressed but no pause menu: give up
// The pause menu's restart handler is run from the UI thread (RequestPauseMenuRestart); if it
// has not been in this long (a pause menu of another kind), B closes the menu and nothing else
// is pressed: never a guess at the menu's items.
constexpr double kDirectTimeout = 1.0;
// START RACE is run directly too; A only if the pre-race screen is still up after all.
constexpr double kTapEvery = 0.6, kLoadTimeout = 30.0, kNoRestart = 8.0;

}  // namespace

void QuickRestart::Go(Step step, double now) {
  step_ = step;
  step_start_ = now;
}

uint16_t QuickRestart::Update(const Frame& f) {
  const double t = f.now - step_start_;
  // Locked through every start sequence and until its GO has left the screen, and whenever
  // the race view is not up (menus, loading, the pre-race screen, replays, photo mode).
  if (f.countdown || !f.race_view) last_countdown_ = f.now;
  const bool locked = f.now - last_countdown_ < kGoOnScreen;
  switch (step_) {
    case Step::kIdle:
      if (trigger_ && f.driving && !locked && (f.pad & trigger_)) Go(Step::kHolding, f.now);
      return 0;
    case Step::kHolding:
      // HOLD: let go (or the race view goes) and it is off; TAP: once pressed it goes on.
      if ((!tap_ && !(f.pad & trigger_)) || !f.driving || locked) {
        Go(Step::kIdle, f.now);
      } else if (t >= (tap_ ? kTapFade : kHoldSeconds)) {
        Go(Step::kPause, f.now);
      }
      return 0;
    case Step::kPause:
      if (t >= kPress && !f.driving) {
        Go(Step::kDirect, f.now);
        return 0;
      }
      if (t >= kPauseTimeout) Go(Step::kRelease, f.now);
      return t < kPress ? kStart : 0;
    case Step::kDirect:
      if (f.direct_done) {
        left_race_view_ = false;
        Go(Step::kLoad, f.now);
      } else if (t >= kDirectTimeout || f.driving) {
        Go(Step::kAbort, f.now);
      }
      return 0;
    case Step::kAbort:
      if (t >= kPress) Go(Step::kRelease, f.now);
      return f.driving ? 0 : kB;  // close the pause menu, if it is still open
    case Step::kLoad:
      if (!f.race_view) left_race_view_ = true;
      // The race view must go (the restart's loading) and come back.
      // Or it went somewhere it cannot finish by itself (a message box, another screen): the loading screen
      // goes and the player has it.
      if ((left_race_view_ && f.race_view && f.driving) || (!left_race_view_ && t >= kNoRestart) ||
          (left_race_view_ && f.menu_waiting) || t >= kLoadTimeout) {
        Go(Step::kRelease, f.now);
        return 0;
      }
      if (!left_race_view_ || !f.prerace_stuck) return 0;
      return std::fmod(t, kTapEvery) < kPress ? kA : 0;
    case Step::kRelease:
      // Nothing again until the button is let go (HOLD would start over at once).
      if (!(f.pad & trigger_)) Go(Step::kIdle, f.now);
      return 0;
  }
  return 0;
}

uint16_t QuickRestartButton(std::string_view name) {
  static constexpr std::array<std::pair<std::string_view, uint16_t>, 2> kNames = {{
      {"dpad_left", kLeft},
      {"dpad_right", kRight},
  }};
  for (const auto& [n, bit] : kNames) {
    if (n == name) return bit;
  }
  return 0;
}

uint16_t ConfiguredQuickRestartButton() {
  return QuickRestartButton(REXCVAR_GET(fm4_quick_restart));
}

bool QuickRestartTap() {
  return REXCVAR_GET(fm4_quick_restart_trigger) == "tap";
}

}  // namespace fm4_static_recomp
