// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "ui/guest_frame_stats.h"

using fm4_static_recomp::ui::GuestFrameStats;
using namespace std::chrono_literals;

TEST_CASE("Guest FPS counts game images independently of UI refresh") {
  GuestFrameStats sampler;
  const auto start = GuestFrameStats::Clock::time_point{};
  sampler.Sample(0, start);
  GuestFrameStats::Stats stats;
  // 120 UI redraws per second, with a new guest image on every other redraw.
  for (int redraw = 1; redraw <= 120; ++redraw) {
    stats = sampler.Sample(redraw / 2, start + std::chrono::nanoseconds(
        int64_t(redraw) * 1'000'000'000 / 120));
  }
  REQUIRE(std::abs(stats.fps - 60.0) < 0.01);
  REQUIRE(std::abs(stats.frame_time_ms - 1000.0 / 60.0) < 0.01);
  REQUIRE(stats.frame_count == 60);
}

TEST_CASE("Guest FPS handles stalled output and counter resets") {
  GuestFrameStats sampler;
  const auto start = GuestFrameStats::Clock::time_point{};
  sampler.Sample(100, start);
  REQUIRE(sampler.Sample(130, start + 250ms).fps == 120.0);
  const auto stalled = sampler.Sample(130, start + 500ms);
  REQUIRE(stalled.fps == 0.0);
  REQUIRE(stalled.frame_time_ms == 0.0);
  REQUIRE(sampler.Sample(0, start + 750ms).fps == 0.0);
  REQUIRE(sampler.Sample(15, start + 1000ms).fps == 60.0);
}

TEST_CASE("Guest FPS windows can fluctuate with perfectly periodic output") {
  GuestFrameStats sampler;
  const auto start = GuestFrameStats::Clock::time_point{};
  sampler.Sample(0, start);
  double lowest = 60.0;
  double highest = 60.0;
  // A perfect 60 Hz producer sampled by a UI with a different phase. Each
  // 260 ms window contains either 15 or 16 images, despite no missed images.
  for (int sample = 1; sample <= 20; ++sample) {
    const auto elapsed = sample * 260ms;
    const uint64_t frames = uint64_t(sample) * 260 * 60 / 1000;
    const auto stats = sampler.Sample(frames, start + elapsed);
    lowest = std::fmin(lowest, stats.fps);
    highest = std::fmax(highest, stats.fps);
    REQUIRE(std::abs(stats.fps - 60.0) < 4.0);
    REQUIRE(stats.frame_count == frames);
  }
  REQUIRE(lowest < 58.0);
  REQUIRE(highest > 61.0);
}

TEST_CASE("Guest FPS average does not describe frame pacing inside its window") {
  GuestFrameStats sampler;
  const auto start = GuestFrameStats::Clock::time_point{};
  sampler.Sample(0, start);
  // A 100 ms stall followed by a burst can still deliver 15 images in 250 ms.
  // The count-only sampler must not pretend its reciprocal average is a
  // measurement of that stall, nor count additional UI draws as guest images.
  sampler.Sample(0, start + 100ms);
  sampler.Sample(15, start + 200ms);
  const auto stats = sampler.Sample(15, start + 250ms);
  REQUIRE(stats.fps == 60.0);
  REQUIRE(std::abs(stats.frame_time_ms - 1000.0 / 60.0) < 0.01);
}
