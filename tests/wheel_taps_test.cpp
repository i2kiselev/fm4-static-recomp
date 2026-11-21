// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include "input/wheel_taps.h"

using fm4_static_recomp::input::WheelTaps;

namespace {
constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008;
constexpr double kPoll = 0.01;  // 100 polls a second

// The taps the game sees over `seconds`: each run of a pressed button counts once.
int Taps(WheelTaps& taps, uint16_t button, double seconds) {
  int count = 0;
  bool was = false;
  for (double t = 0; t < seconds; t += kPoll) {
    const bool down = (taps.Buttons(kPoll) & button) != 0;
    if (down && !was) ++count;
    was = down;
  }
  return count;
}
}  // namespace

TEST_CASE("Each wheel notch is one D-pad tap, with a release between taps", "[wheel_taps]") {
  WheelTaps taps;
  taps.AddNotches(3, false);
  CHECK(Taps(taps, kUp, 1.0) == 3);
  CHECK(taps.Buttons(kPoll) == 0);  // nothing left
}

TEST_CASE("Down, and Shift for left and right", "[wheel_taps]") {
  WheelTaps taps;
  taps.AddNotches(-2, false);
  CHECK(Taps(taps, kDown, 1.0) == 2);
  taps.AddNotches(1, true);
  CHECK(Taps(taps, kLeft, 1.0) == 1);
  taps.AddNotches(-1, true);
  CHECK(Taps(taps, kRight, 1.0) == 1);
}

TEST_CASE("A fast spin is capped, and turning back drops the queue", "[wheel_taps]") {
  WheelTaps taps;
  taps.AddNotches(30, false);
  CHECK(Taps(taps, kUp, 3.0) == int(WheelTaps::kMaxQueued));
  taps.AddNotches(5, false);
  taps.Buttons(kPoll);  // the first up tap starts
  taps.AddNotches(-1, false);
  int ups = 0, downs = 0;
  bool up_was = true, down_was = false;
  for (double t = 0; t < 1.0; t += kPoll) {
    const uint16_t b = taps.Buttons(kPoll);
    if ((b & kUp) && !up_was) ++ups;
    if ((b & kDown) && !down_was) ++downs;
    up_was = b & kUp;
    down_was = b & kDown;
  }
  CHECK(ups == 0);  // the held tap finishes, the queued ups are dropped
  CHECK(downs == 1);
}

TEST_CASE("Clear drops a held tap", "[wheel_taps]") {
  WheelTaps taps;
  taps.AddNotches(2, false);
  CHECK(taps.Buttons(kPoll) == kUp);
  taps.Clear();
  CHECK(taps.Buttons(kPoll) == 0);
}

TEST_CASE("A left click is an A tap, a right click a B tap, in order with the wheel", "[wheel_taps]") {
  WheelTaps taps;
  taps.AddNotches(-1, false);
  taps.AddClick(false);
  taps.AddClick(true);
  CHECK(Taps(taps, 0x0002, 0.1) == 1);
  CHECK(Taps(taps, 0x1000, 0.1) == 1);
  CHECK(Taps(taps, 0x2000, 0.1) == 1);
}

TEST_CASE("The click that focuses the window is not a menu click", "[wheel_taps]") {
  using fm4_static_recomp::input::MenuMouseKeys;
  MenuMouseKeys keys;
  const auto t0 = MenuMouseKeys::Clock::now();
  CHECK(keys.ClickCounts(t0));  // never focused yet: clicks count
  keys.Focused(t0);
  CHECK_FALSE(keys.ClickCounts(t0 + std::chrono::milliseconds(50)));
  CHECK_FALSE(keys.ClickCounts(t0 + std::chrono::milliseconds(299)));
  CHECK(keys.ClickCounts(t0 + std::chrono::milliseconds(300)));
}

TEST_CASE("Shift is tracked for the wheel and does not take the prompts back", "[wheel_taps]") {
  fm4_static_recomp::input::MenuMouseKeys keys;
  CHECK_FALSE(keys.shift());
  CHECK_FALSE(keys.Key(true, true));  // Shift down: not "another key"
  CHECK(keys.shift());
  CHECK(keys.Key(false, true));  // any other key down: the prompts go back to the keys
  CHECK_FALSE(keys.Key(false, false));
  CHECK_FALSE(keys.Key(true, false));
  CHECK_FALSE(keys.shift());
}
