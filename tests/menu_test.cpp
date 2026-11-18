// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "ui/hostui/menu.h"

namespace {

using fm4_static_recomp::hostui::kAdjustCycle;
using fm4_static_recomp::hostui::MenuRow;
using fm4_static_recomp::hostui::MenuScreen;
using fm4_static_recomp::hostui::MenuSound;
using fm4_static_recomp::hostui::NavCommand;
using fm4_static_recomp::hostui::NavOutcome;
using fm4_static_recomp::hostui::NavSound;
using fm4_static_recomp::hostui::StepIndex;

}  // namespace

TEST_CASE("Left and right stop at the first and last value, accept rolls over", "[menu]") {
  CHECK(StepIndex(0, 3, -1) == 0);
  CHECK(StepIndex(0, 3, 1) == 1);
  CHECK(StepIndex(2, 3, 1) == 2);
  CHECK(StepIndex(2, 3, -1) == 1);
  CHECK(StepIndex(2, 3, kAdjustCycle) == 0);
  CHECK(StepIndex(0, 3, kAdjustCycle) == 1);
  // No value matching (CUSTOM): the first going right or on accept, the last going left.
  CHECK(StepIndex(-1, 3, 1) == 0);
  CHECK(StepIndex(-1, 3, kAdjustCycle) == 0);
  CHECK(StepIndex(-1, 3, -1) == 2);
  CHECK(StepIndex(0, 0, 1) == -1);
}

TEST_CASE("A row gets -1 and +1 from left and right and kAdjustCycle from accept", "[menu]") {
  std::vector<int> got;
  MenuRow row;
  row.label = "ROW";
  row.value = [] { return std::string("V"); };
  row.adjust = [&got](int direction) { got.push_back(direction); };
  MenuScreen screen("TEST", {row});
  screen.Handle(NavCommand::kLeft);
  screen.Handle(NavCommand::kRight);
  screen.Handle(NavCommand::kAccept);
  CHECK(got == std::vector<int>{-1, 1, kAdjustCycle});
}

TEST_CASE("Moving, changing and stopping at an end each have the game's sound", "[menu]") {
  CHECK(NavSound({.command = NavCommand::kDown, .handled = true, .focus_moved = true}) ==
        MenuSound::kMove);
  CHECK_FALSE(NavSound({.command = NavCommand::kDown}));
  CHECK(NavSound({.command = NavCommand::kRight, .handled = true, .value_changed = true}) ==
        MenuSound::kChange);
  CHECK(NavSound({.command = NavCommand::kLeft, .handled = true}) == MenuSound::kEnd);
  CHECK(NavSound({.command = NavCommand::kAccept, .handled = true, .value_changed = true}) ==
        MenuSound::kChange);
  CHECK_FALSE(NavSound({.command = NavCommand::kAccept, .handled = true}));
}

TEST_CASE("Sliders, slots, applying and leaving have theirs", "[menu]") {
  CHECK(NavSound({.command = NavCommand::kRight, .value_changed = true, .slider = true}) ==
        MenuSound::kSlideUp);
  CHECK(NavSound({.command = NavCommand::kLeft, .value_changed = true, .slider = true}) ==
        MenuSound::kSlideDown);
  CHECK(NavSound({.command = NavCommand::kLeft, .slider = true}) == MenuSound::kSlideEnd);
  CHECK(NavSound({.command = NavCommand::kRight, .handled = true, .value_changed = true,
                  .slots = true}) == MenuSound::kMove);
  CHECK(NavSound({.command = NavCommand::kAccept, .handled = true, .slots = true}) ==
        MenuSound::kAccept);
  CHECK(NavSound({.command = NavCommand::kApply, .handled = true}) == MenuSound::kAccept);
  CHECK_FALSE(NavSound({.command = NavCommand::kApply}));
  CHECK(NavSound({.command = NavCommand::kBack, .left_screen = true}) == MenuSound::kBack);
  // The screen kept open (unapplied changes, asked to confirm).
  CHECK(NavSound({.command = NavCommand::kBack, .handled = true}) == MenuSound::kError);
  // A key capture: only its cancel sounds.
  CHECK(NavSound({.command = NavCommand::kBack, .handled = true, .capturing = true}) ==
        MenuSound::kBack);
  CHECK_FALSE(NavSound({.command = NavCommand::kDown, .capturing = true}));
}
