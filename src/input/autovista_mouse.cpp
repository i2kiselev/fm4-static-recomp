// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "input/autovista_mouse.h"

#include <algorithm>
#include <cmath>

namespace fm4_static_recomp::input {

namespace {

double Clamp1(double v) { return std::clamp(v, -1.0, 1.0); }

bool Beyond(UiPoint a, UiPoint b, float slop) { return std::fabs(a.x - b.x) > slop || std::fabs(a.y - b.y) > slop; }

}  // namespace

void AutovistaMouse::Press(bool right, UiPoint at) {
  Button& button = right ? right_ : left_;
  button = Button{true, false, at};
  at_ = walked_ = looked_ = at;
  if (right) {
    look_x_ = look_y_ = 0;
  } else {
    walk_x_ = walk_y_ = 0;
  }
}

void AutovistaMouse::Move(UiPoint at) {
  at_ = at;
  for (Button* button : {&left_, &right_}) {
    if (button->down && Beyond(at, button->pressed, kClickSlop)) button->dragged = true;
  }
}

bool AutovistaMouse::Release(bool right) {
  Button& button = right ? right_ : left_;
  const bool click = button.down && !button.dragged;
  button = Button{};
  if (!right) walk_x_ = walk_y_ = 0;
  return click;
}

void AutovistaMouse::Notches(int notches) {
  // A turn the other way first cancels what is left of the last.
  if ((notches > 0) != (notch_seconds_ > 0)) notch_seconds_ = 0;
  notch_seconds_ += notches * kNotchSeconds;
}

Sticks AutovistaMouse::Update(double elapsed) {
  Sticks sticks;
  if (elapsed <= 0) elapsed = 1e-3;
  if (left_.down) {
    // The pointer's speed since the last poll, smoothed (polls see uneven slices of the motion).
    const double vx = (at_.x - walked_.x) / elapsed, vy = (at_.y - walked_.y) / elapsed;
    walked_ = at_;
    const double k = 1 - std::exp(-elapsed / kWalkSmoothing);
    walk_x_ += (vx - walk_x_) * k;
    walk_y_ += (vy - walk_y_) * k;
    // The car follows the pointer: a drag right walks left around it.
    const double full = look_by_speed_ ? kPhotoWalkFullSpeed : kWalkFullSpeed;
    sticks.lx = Clamp1(-walk_x_ / full);
    sticks.ly = Clamp1(walk_y_ / full);
  }
  if (notch_seconds_ != 0) {
    const double step = std::min(std::fabs(notch_seconds_), elapsed);
    const double sign = notch_seconds_ > 0 ? 1 : -1;
    // A step shorter than the poll still moves the stick fully (the game sees whole polls).
    sticks.ly = sign;
    notch_seconds_ -= sign * step;
    if (std::fabs(notch_seconds_) < 1e-9) notch_seconds_ = 0;
  }
  if (right_.down && look_by_speed_) {
    const double vx = (at_.x - looked_.x) / elapsed, vy = (at_.y - looked_.y) / elapsed;
    looked_ = at_;
    const double k = 1 - std::exp(-elapsed / kWalkSmoothing);
    look_x_ += (vx - look_x_) * k;
    look_y_ += (vy - look_y_) * k;
    sticks.rx = Clamp1(look_x_ / kLookFullSpeed);
    sticks.ry = Clamp1(look_y_ / kLookFullSpeed);
  } else if (right_.down) {
    sticks.rx = Clamp1((at_.x - right_.pressed.x) / kLookRange);
    sticks.ry = Clamp1((at_.y - right_.pressed.y) / kLookRange);
  }
  return sticks;
}

void AutovistaMouse::Reset() { *this = AutovistaMouse{}; }

}  // namespace fm4_static_recomp::input
