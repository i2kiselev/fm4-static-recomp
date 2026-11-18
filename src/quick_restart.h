// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <string_view>

namespace fm4_static_recomp {

// QUICK RESTART: a pad button restarts the race while driving. It presses START; once the
// pause menu is up its own "restart confirmed" handler runs (RequestPauseMenuRestart), and
// once the pre-race screen is up its START RACE (RequestPreRaceStart), under a loading screen
// (ui/quick_restart_screen.h). This is the input side: what to press and when, from what the
// game is showing.
class QuickRestart {
 public:
  struct Frame {
    double now = 0;              // seconds, any steady origin
    bool driving = false;        // on track: no race menu, no host page over the game
    bool race_view = false;      // the race render scenario is the last one pushed
    bool countdown = false;      // that race view is still the start sequence, before GO
    uint16_t pad = 0;            // the player's own pad buttons
    bool direct_done = false;    // the pause menu's restart handler has run (wants_direct)
    bool prerace_stuck = false;  // the pre-race screen is still up a while after START RACE
  };

  // HOLD: held this long while the screen fades out; letting go before cancels.
  static constexpr double kHoldSeconds = 0.35;
  // TAP: one press, then this fade, with no cancel.
  static constexpr double kTapFade = 0.3;
  // After a start sequence, GO! stays up this long: no restart until then.
  static constexpr double kGoOnScreen = 2.0;

  // Called for every read of the player's pad; returns the buttons to press on top of theirs.
  uint16_t Update(const Frame& frame);

  // The button that triggers it (X_INPUT_GAMEPAD_*), 0 for off; a restart going on finishes.
  void set_trigger(uint16_t trigger) { trigger_ = trigger; }
  void set_tap(bool tap) { tap_ = tap; }
  bool enabled() const { return trigger_ != 0; }

  // Armed: HOLD's button held, or TAP's fade, before START is pressed.
  bool holding() const { return step_ == Step::kHolding; }
  // The pause menu is up: its "restart confirmed" handler should run.
  bool wants_direct() const { return step_ == Step::kDirect; }
  // The race is reloading: the pre-race screen's START RACE should run as soon as it is up.
  bool wants_start() const { return step_ == Step::kLoad; }
  bool running() const {
    return step_ == Step::kPause || step_ == Step::kDirect || step_ == Step::kAbort ||
           step_ == Step::kLoad;
  }

 private:
  enum class Step { kIdle, kHolding, kPause, kDirect, kAbort, kLoad, kRelease };

  void Go(Step step, double now);

  uint16_t trigger_ = 0;
  bool tap_ = false;
  Step step_ = Step::kIdle;
  double step_start_ = 0;
  bool left_race_view_ = false;
  double last_countdown_ = -1e9;
};

// fm4_quick_restart's button names (dpad_left, dpad_right: the D-pad sides no controller
// layout uses while driving) as X_INPUT_GAMEPAD_* bits; 0 for "off" or an unknown name.
uint16_t QuickRestartButton(std::string_view name);
// The button fm4_quick_restart sets now, and whether fm4_quick_restart_trigger is "tap".
uint16_t ConfiguredQuickRestartButton();
bool QuickRestartTap();

}  // namespace fm4_static_recomp
