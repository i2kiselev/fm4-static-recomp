// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "input/keyboard_ramp.h"

#include <algorithm>
#include <cmath>

namespace fm4_static_recomp::input {
namespace {

// Moves value toward target over elapsed seconds, rise_seconds from rest to
// full scale and return_seconds back; a value on the other side of zero
// returns to zero first. Time left over at zero is spent rising.
double MoveAxis(double value, double target, double elapsed, double rise_seconds,
                double return_seconds) {
  while (value != target) {
    const bool returning =
        value != 0.0 && (target * value <= 0.0 || std::abs(target) < std::abs(value));
    const double goal = returning && target * value < 0.0 ? 0.0 : target;
    const double seconds = returning ? return_seconds : rise_seconds;
    const double distance = std::abs(goal - value);
    if (seconds <= 0.0) {
      value = goal;
      continue;
    }
    const double travel = elapsed / seconds;
    if (travel < distance) {
      return value + std::copysign(travel, goal - value);
    }
    value = goal;
    elapsed -= distance * seconds;
  }
  return value;
}

double Stick(int16_t value) {
  return value / 32767.0;
}
int16_t ToStick(double value) {
  return int16_t(std::lround(std::clamp(value, -1.0, 1.0) * 32767.0));
}
double Trigger(uint8_t value) {
  return value / 255.0;
}
uint8_t ToTrigger(double value) {
  return uint8_t(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}

}  // namespace

KeyboardRamp::KeyboardRamp(RampRates rates) : rates_(rates) {}

void KeyboardRamp::Apply(rex::input::X_INPUT_GAMEPAD& pad, double elapsed_seconds, bool driving,
                         bool mouse_look) {
  const double steer = Stick(pad.thumb_lx);
  const double left_trigger = Trigger(pad.left_trigger);
  const double right_trigger = Trigger(pad.right_trigger);
  const double look_x = Stick(pad.thumb_rx);
  const double look_y = Stick(pad.thumb_ry);
  if (!driving) {
    steer_ = steer;
    left_trigger_ = left_trigger;
    right_trigger_ = right_trigger;
    look_x_ = look_x;
    look_y_ = look_y;
    return;
  }
  const double elapsed = std::clamp(elapsed_seconds, 0.0, 0.1);
  steer_ = MoveAxis(steer_, steer, elapsed, rates_.steer_seconds, rates_.steer_return_seconds);
  left_trigger_ = MoveAxis(left_trigger_, left_trigger, elapsed, rates_.pedal_seconds,
                           rates_.pedal_return_seconds);
  right_trigger_ = MoveAxis(right_trigger_, right_trigger, elapsed, rates_.pedal_seconds,
                            rates_.pedal_return_seconds);
  pad.thumb_lx = ToStick(steer_);
  pad.left_trigger = ToTrigger(left_trigger_);
  pad.right_trigger = ToTrigger(right_trigger_);
  if (mouse_look) {
    // Released, the look keys rise from rest.
    look_x_ = 0.0;
    look_y_ = 0.0;
    return;
  }
  look_x_ = MoveAxis(look_x_, look_x, elapsed, rates_.look_seconds, rates_.look_seconds);
  look_y_ = MoveAxis(look_y_, look_y, elapsed, rates_.look_seconds, rates_.look_seconds);
  pad.thumb_rx = ToStick(look_x_);
  pad.thumb_ry = ToStick(look_y_);
}

}  // namespace fm4_static_recomp::input
