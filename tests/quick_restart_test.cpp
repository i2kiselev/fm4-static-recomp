// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "quick_restart.h"

namespace {

using fm4_static_recomp::QuickRestart;
using fm4_static_recomp::QuickRestartButton;

constexpr uint16_t kLeft = 0x0004, kRight = 0x0008, kStart = 0x0010, kA = 0x1000, kB = 0x2000;
constexpr double kStep = 0.01;

// A race being driven, with the pad read every 10 ms; the fields are what the game shows.
struct Race {
  explicit Race(bool tap = false) {
    quick.set_trigger(kLeft);
    quick.set_tap(tap);
  }

  // Runs for `seconds`, ORing up the buttons pressed in that time.
  uint16_t Run(double seconds) {
    uint16_t pressed = 0;
    for (const double end = frame.now + seconds; frame.now < end - kStep / 2; frame.now += kStep) {
      pressed = uint16_t(pressed | quick.Update(frame));
    }
    return pressed;
  }

  QuickRestart quick;
  QuickRestart::Frame frame{.now = 100, .driving = true, .race_view = true};
};

// START goes in; the pause menu opens and its restart handler runs; the race reloads.
void PauseAndReload(Race& race) {
  CHECK(race.Run(0.2) & kStart);
  race.frame.driving = false;  // the pause menu
  race.Run(0.05);
  CHECK(race.quick.wants_direct());
  race.frame.direct_done = true;
  race.Run(0.05);
  CHECK(race.quick.wants_start());
  race.frame.direct_done = false;
  race.frame.race_view = false;  // loading, the pre-race screen
  race.Run(1.0);
  race.frame.race_view = true;
  race.frame.countdown = true;
  race.frame.driving = true;
  race.Run(0.05);
}

}  // namespace

TEST_CASE("Only the two D-pad sides name a button", "[quick_restart]") {
  CHECK(QuickRestartButton("dpad_left") == kLeft);
  CHECK(QuickRestartButton("dpad_right") == kRight);
  CHECK(QuickRestartButton("off") == 0);
  CHECK(QuickRestartButton("x") == 0);
  CHECK(QuickRestartButton("") == 0);
}

TEST_CASE("Off presses nothing", "[quick_restart]") {
  Race race;
  race.quick.set_trigger(0);
  CHECK_FALSE(race.quick.enabled());
  race.frame.pad = kLeft;
  CHECK(race.Run(2.0) == 0);
  CHECK_FALSE(race.quick.holding());
}

TEST_CASE("HOLD restarts once the button is held through the fade", "[quick_restart]") {
  Race race;
  race.frame.pad = kLeft;
  CHECK(race.Run(QuickRestart::kHoldSeconds - 0.05) == 0);
  CHECK(race.quick.holding());
  PauseAndReload(race);
  CHECK_FALSE(race.quick.running());
  // Still held: nothing starts again until it is let go.
  CHECK(race.Run(5.0) == 0);
  CHECK_FALSE(race.quick.holding());
  race.frame.pad = 0;
  race.frame.countdown = false;
  race.Run(QuickRestart::kGoOnScreen + 0.1);
  race.frame.pad = kLeft;
  race.Run(0.05);
  CHECK(race.quick.holding());
}

TEST_CASE("HOLD let go during the fade cancels", "[quick_restart]") {
  Race race;
  race.frame.pad = kLeft;
  race.Run(0.2);
  CHECK(race.quick.holding());
  race.frame.pad = 0;
  CHECK(race.Run(1.0) == 0);
  CHECK_FALSE(race.quick.holding());
  CHECK_FALSE(race.quick.running());
}

TEST_CASE("TAP restarts from one press", "[quick_restart]") {
  Race race(true);
  race.frame.pad = kLeft;
  race.Run(0.03);
  race.frame.pad = 0;
  CHECK(race.quick.holding());
  // Let go: the fade goes on all the same.
  CHECK(race.Run(QuickRestart::kTapFade - 0.08) == 0);
  CHECK(race.quick.holding());
  PauseAndReload(race);
  CHECK_FALSE(race.quick.running());
}

TEST_CASE("Nothing during a start sequence, until GO has gone, or off the track",
          "[quick_restart]") {
  Race race;
  race.frame.countdown = true;
  race.frame.pad = kLeft;
  CHECK(race.Run(2.0) == 0);
  race.frame.countdown = false;
  CHECK(race.Run(QuickRestart::kGoOnScreen - 0.1) == 0);
  CHECK_FALSE(race.quick.holding());
  race.frame.pad = 0;
  race.Run(0.2);
  race.frame.pad = kLeft;
  race.Run(0.05);
  CHECK(race.quick.holding());

  Race menu;
  menu.frame.driving = false;
  menu.frame.pad = kLeft;
  CHECK(menu.Run(2.0) == 0);
  menu.frame.driving = true;
  menu.frame.race_view = false;  // a replay, photo mode
  CHECK(menu.Run(2.0) == 0);
  CHECK_FALSE(menu.quick.holding());
}

TEST_CASE("A pause menu whose restart never runs is closed with B", "[quick_restart]") {
  Race race;
  race.frame.pad = kLeft;
  race.Run(QuickRestart::kHoldSeconds);
  CHECK(race.Run(0.2) & kStart);
  race.frame.driving = false;
  race.Run(0.05);
  CHECK(race.quick.wants_direct());
  const uint16_t pressed = race.Run(1.2);
  CHECK(pressed & kB);
  CHECK_FALSE(pressed & kA);
  CHECK_FALSE(race.quick.running());
}

TEST_CASE("A pre-race screen still up after START RACE gets A", "[quick_restart]") {
  Race race;
  race.frame.pad = kLeft;
  race.Run(QuickRestart::kHoldSeconds);
  race.Run(0.2);
  race.frame.driving = false;
  race.Run(0.05);
  race.frame.direct_done = true;
  race.Run(0.05);
  race.frame.direct_done = false;
  race.frame.race_view = false;
  CHECK_FALSE(race.Run(1.0) & kA);
  race.frame.prerace_stuck = true;
  CHECK(race.Run(1.0) & kA);
  race.frame.prerace_stuck = false;
  race.frame.race_view = true;
  race.frame.driving = true;
  race.Run(0.05);
  CHECK_FALSE(race.quick.running());
}
