// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <rex/input/input.h>

#include "input/controls.h"

namespace fm4_static_recomp::input {

// Smooths the keyboard's all-or-nothing steering, pedals and look while
// driving: each axis moves toward the keys' value at full scale per its rate,
// back toward zero at the return rate, and through zero first on a reversal.
// Menus pass the keys through unchanged, and so does the look while the mouse
// turns it (mouse_look): ramping each axis would shorten a turned stick under
// the game's look threshold.
class KeyboardRamp {
 public:
  explicit KeyboardRamp(RampRates rates);
  void SetRates(const RampRates& rates) { rates_ = rates; }
  void Apply(rex::input::X_INPUT_GAMEPAD& pad, double elapsed_seconds, bool driving,
             bool mouse_look = false);

 private:
  RampRates rates_;
  double steer_ = 0.0;
  double left_trigger_ = 0.0;
  double right_trigger_ = 0.0;
  double look_x_ = 0.0;
  double look_y_ = 0.0;
};

}  // namespace fm4_static_recomp::input
