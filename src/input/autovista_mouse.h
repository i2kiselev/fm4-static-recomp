// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include "input/menu_hit.h"

namespace fm4_static_recomp::input {

// The mouse in the Autovista viewer (#76). Its camera walks a Kinect body around the car; with a pad the
// left stick walks it (sideways around the car, up closer, down back) and the right stick turns its head,
// which springs back at rest (WalkaroundCamera.ini, Controller*). The mouse works those sticks, so the
// camera keeps the game's own smoothing, limits and collision:
//   left drag   walks: the car follows the pointer sideways, up steps closer, down steps back; as fast as
//               the pointer moves, stopping when it stops
//   wheel       a short step closer (up) or back (down) a notch
//   right drag  turns the head as far as the pointer is from where it was pressed
// A press let go where it was pressed is a click (the viewer's A or B), not a drag.
// The UI photo mode (TAKE PHOTO) is worked the same way, but its right stick turns the view at a speed and
// leaves it there (SetPhotoMode): a right drag turns it as fast as the pointer moves.
struct Sticks {
  double lx = 0, ly = 0, rx = 0, ry = 0;  // -1..1, y up
};

inline constexpr float kWalkFullSpeed = 900;   // UI units a second for a full stick
inline constexpr double kWalkSmoothing = 0.06;  // seconds: the walk follows the pointer's speed this closely
inline constexpr float kLookRange = 240;        // UI units from the press for a full head turn
inline constexpr float kLookFullSpeed = 250;   // photo mode: UI units a second for a full turning stick
inline constexpr float kPhotoWalkFullSpeed = 300;  // photo mode moves slower: a full stick sooner
inline constexpr float kClickSlop = 8;          // UI units a click may move
inline constexpr double kNotchSeconds = 0.15;   // a wheel notch: a full stick this long

// Not thread safe: the caller holds a lock (window thread events, pad filter polls).
class AutovistaMouse {
 public:
  void Press(bool right, UiPoint at);
  void Move(UiPoint at);
  // True when it is a click: pressed here, never moved past kClickSlop.
  bool Release(bool right);
  void Notches(int notches);
  // The sticks for a pad poll `elapsed` seconds after the last.
  Sticks Update(double elapsed);
  void Reset();
  // Photo mode: the look turns by the pointer's speed, and the walk takes a full stick sooner.
  void SetPhotoMode(bool photo) { look_by_speed_ = photo; }

 private:
  struct Button {
    bool down = false;
    bool dragged = false;
    UiPoint pressed;
  };
  Button left_, right_;
  UiPoint at_;
  UiPoint walked_;  // the pointer where the last poll saw it, for the walk's speed
  double walk_x_ = 0, walk_y_ = 0;  // smoothed pointer speed, UI units a second
  UiPoint looked_;                  // photo mode: the pointer where the last poll saw it
  double look_x_ = 0, look_y_ = 0;  // photo mode: smoothed pointer speed while right dragging
  bool look_by_speed_ = false;
  double notch_seconds_ = 0;        // signed: closer (+) or back (-) still to play
};

}  // namespace fm4_static_recomp::input
