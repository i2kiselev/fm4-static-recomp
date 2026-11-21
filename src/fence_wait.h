// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>

namespace fm4_static_recomp {

// The game's render-fence wait (1.0 822B0788, Rev 1 822B06B8) polls until the GPU completes a
// fence, with delay hints that on the console hand the core to its sibling hardware thread and
// here return at once: one core at 100 % for as long as the game runs (#111). fm4_guest_fence_wait
// lets a wait spin as before for kSpinUs, then sleeps kSleepUs per loop pass until it ends.
// Values from the #111 proposal: a 500 us step saved no more power and lengthened slow frames.
class FenceWaitBackoff {
 public:
  static constexpr int64_t kSpinUs = 50;
  static constexpr int64_t kSleepUs = 100;

  // At each loop pass, with the steady clock in microseconds: true to sleep one step now.
  bool ShouldSleep(int64_t now_us) {
    // One pass takes well under a microsecond; a longer gap since the last one is a new wait.
    if (now_us - last_pass_us_ > kNewWaitGapUs) {
      wait_start_us_ = now_us;
    }
    last_pass_us_ = now_us;
    return now_us - wait_start_us_ > kSpinUs;
  }

  // After the sleep, so the sleep itself does not read as the start of a new wait.
  void Slept(int64_t now_us) { last_pass_us_ = now_us; }

 private:
  static constexpr int64_t kNewWaitGapUs = 20;
  int64_t wait_start_us_ = 0;
  int64_t last_pass_us_ = INT64_MIN / 2;
};

}  // namespace fm4_static_recomp
