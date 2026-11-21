// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "input/autovista_mouse.h"

using namespace fm4_static_recomp::input;
using Catch::Approx;

namespace {

// Moves the pointer by (dx, dy) over `seconds`, in pad polls of 1/120 s, and returns the last sticks.
Sticks DragBy(AutovistaMouse& mouse, UiPoint& at, float dx, float dy, double seconds) {
  const int polls = int(seconds * 120 + 0.5);
  Sticks sticks;
  for (int i = 0; i < polls; ++i) {
    at.x += dx / polls;
    at.y += dy / polls;
    mouse.Move(at);
    sticks = mouse.Update(1.0 / 120);
  }
  return sticks;
}

}  // namespace

TEST_CASE("Autovista: nothing pressed leaves the sticks alone", "[autovista_mouse]") {
  AutovistaMouse mouse;
  mouse.Move({10, 10});
  mouse.Move({300, -200});
  const Sticks sticks = mouse.Update(1.0 / 60);
  CHECK(sticks.lx == 0);
  CHECK(sticks.ly == 0);
  CHECK(sticks.rx == 0);
  CHECK(sticks.ry == 0);
}

TEST_CASE("Autovista: a left drag walks around the car, the car following the pointer", "[autovista_mouse]") {
  AutovistaMouse mouse;
  UiPoint at{0, 0};
  mouse.Press(false, at);
  // Right at half the full speed: the walk goes left (the car turns right, with the pointer).
  const Sticks right = DragBy(mouse, at, kWalkFullSpeed * 0.25f, 0, 0.5);  // 450 a second
  CHECK(right.lx == Approx(-0.5).margin(0.05));
  CHECK(right.ly == Approx(0).margin(0.01));
  // Far faster than full speed: held to the stick's end.
  const Sticks fast = DragBy(mouse, at, -kWalkFullSpeed * 3, 0, 0.5);
  CHECK(fast.lx == Approx(1));
}

TEST_CASE("Autovista: a left drag up steps closer, down steps back", "[autovista_mouse]") {
  AutovistaMouse mouse;
  UiPoint at{0, 0};
  mouse.Press(false, at);
  CHECK(DragBy(mouse, at, 0, kWalkFullSpeed * 0.25f, 0.5).ly == Approx(0.5).margin(0.05));
  CHECK(DragBy(mouse, at, 0, -kWalkFullSpeed * 0.25f, 0.5).ly == Approx(-0.5).margin(0.05));
}

TEST_CASE("Autovista: the walk stops when the pointer stops or the button is let go", "[autovista_mouse]") {
  AutovistaMouse mouse;
  UiPoint at{0, 0};
  mouse.Press(false, at);
  DragBy(mouse, at, kWalkFullSpeed, 0, 0.5);
  // Held still: the stick settles back to rest within a few tenths of a second.
  Sticks still;
  for (int i = 0; i < 60; ++i) still = mouse.Update(1.0 / 120);
  CHECK(std::abs(still.lx) < 0.02);
  // Let go mid-drag: at rest at once.
  DragBy(mouse, at, kWalkFullSpeed, 0, 0.5);
  mouse.Release(false);
  CHECK(mouse.Update(1.0 / 120).lx == 0);
}

TEST_CASE("Autovista: a right drag turns the head as far as the pointer is from where it was pressed",
          "[autovista_mouse]") {
  AutovistaMouse mouse;
  mouse.Press(true, {100, 50});
  mouse.Move({100 + kLookRange * 0.5f, 50 + kLookRange * 0.25f});
  Sticks sticks = mouse.Update(1.0 / 60);
  CHECK(sticks.rx == Approx(0.5));
  CHECK(sticks.ry == Approx(0.25));  // pointer up, look up
  CHECK(sticks.lx == 0);
  // Held there: still looking (the pointer's place, not its speed).
  sticks = mouse.Update(1.0 / 60);
  CHECK(sticks.rx == Approx(0.5));
  mouse.Move({100 - kLookRange * 2, 50});
  CHECK(mouse.Update(1.0 / 60).rx == Approx(-1));
  // Let go: the head comes back (the game springs it back from a stick at rest).
  mouse.Release(true);
  CHECK(mouse.Update(1.0 / 60).rx == 0);
}

TEST_CASE("Photo mode: a right drag turns the view as fast as the pointer moves", "[autovista_mouse]") {
  // Its look stays where it is left (no spring): the stick is a turning speed, so the pointer's speed drives it.
  AutovistaMouse mouse;
  mouse.SetPhotoMode(true);
  UiPoint at{0, 0};
  mouse.Press(true, at);
  Sticks sticks;
  for (int i = 0; i < 60; ++i) {
    at.x += kLookFullSpeed * 0.5f / 120;
    at.y -= kLookFullSpeed * 0.25f / 120;
    mouse.Move(at);
    sticks = mouse.Update(1.0 / 120);
  }
  CHECK(sticks.rx == Approx(0.5).margin(0.05));
  CHECK(sticks.ry == Approx(-0.25).margin(0.05));  // pointer down, look down
  // Held still: it stops turning.
  for (int i = 0; i < 60; ++i) sticks = mouse.Update(1.0 / 120);
  CHECK(std::abs(sticks.rx) < 0.02);
  mouse.Release(true);
  CHECK(mouse.Update(1.0 / 120).rx == 0);
}

TEST_CASE("Autovista: a press let go where it was pressed is a click, not a drag", "[autovista_mouse]") {
  AutovistaMouse mouse;
  mouse.Press(false, {0, 0});
  mouse.Move({kClickSlop * 0.5f, 0});
  CHECK(mouse.Release(false));
  mouse.Press(true, {0, 0});
  CHECK(mouse.Release(true));
  // Moved past the slop, even back again: a drag.
  mouse.Press(false, {0, 0});
  mouse.Move({kClickSlop * 2, 0});
  mouse.Move({0, 0});
  CHECK_FALSE(mouse.Release(false));
  // A release without a press (pressed outside the viewer) is no click.
  CHECK_FALSE(mouse.Release(true));
}

TEST_CASE("Autovista: a wheel notch is a short step closer or back", "[autovista_mouse]") {
  AutovistaMouse mouse;
  mouse.Notches(1);
  CHECK(mouse.Update(0.01).ly == 1);
  // The notch's step lasts kNotchSeconds, then the stick is back at rest.
  double held = 0.01;
  while (mouse.Update(0.01).ly == 1) held += 0.01;
  CHECK(held == Approx(kNotchSeconds).margin(0.011));
  CHECK(mouse.Update(0.01).ly == 0);
  // Notches add up; down goes back.
  mouse.Notches(-2);
  CHECK(mouse.Update(kNotchSeconds).ly == -1);
  CHECK(mouse.Update(kNotchSeconds * 0.9).ly == -1);
  CHECK(mouse.Update(0.2).ly == -1);  // the last sliver
  CHECK(mouse.Update(0.01).ly == 0);
  // Up after down: what was left of the down is dropped.
  mouse.Notches(-1);
  mouse.Notches(1);
  CHECK(mouse.Update(kNotchSeconds).ly == 1);
  CHECK(mouse.Update(0.01).ly == 0);
}

TEST_CASE("Autovista: Reset forgets presses, drags and notches", "[autovista_mouse]") {
  AutovistaMouse mouse;
  UiPoint at{0, 0};
  mouse.Press(false, at);
  DragBy(mouse, at, kWalkFullSpeed, 0, 0.2);
  mouse.Notches(3);
  mouse.Reset();
  const Sticks sticks = mouse.Update(1.0 / 60);
  CHECK(sticks.lx == 0);
  CHECK(sticks.ly == 0);
  CHECK_FALSE(mouse.Release(false));
}
