// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
//
// The render thread's wait for GPU ring-buffer space (#111). sub_822B0788 loops at 0x822B0818, calling
// sub_826E8358, which spins on db16cyc delay hints until the GPU's read pointer moves. On the console those hints
// hand the core to the sibling hardware thread; here the loop polls flat out, one core at 100 % in every scene.
// Fm4RingWaitBackoff runs at the loop head and never changes its course: a wait shorter than kSpinUs spins as
// before, a longer one sleeps in kSleepUs high-resolution steps, so the core can idle while the GPU catches up.

#include <cstdint>

#include <rex/cvar.h>
#include <rex/ppc/context.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(fm4_ring_wait_backoff, true, "Forza Motorsport 4",
                    "The render thread sleeps in short steps while it waits for the GPU instead of polling one "
                    "core at 100 %; false polls as before");

namespace {

constexpr int64_t kSpinUs = 50;   // a wait this short spins, as before
constexpr int64_t kSleepUs = 100;  // then each step sleeps this long
constexpr int64_t kNewWaitUs = 20;  // a longer gap between two loop turns is a new wait

#if defined(_WIN32)
int64_t NowUs() {
  static const int64_t frequency = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return int64_t(f.QuadPart);
  }();
  LARGE_INTEGER counter;
  QueryPerformanceCounter(&counter);
  return int64_t(counter.QuadPart) * 1000000 / frequency;
}

void SleepUs(int64_t us) {
  thread_local HANDLE timer =
      CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (!timer) {
    SwitchToThread();
    return;
  }
  LARGE_INTEGER due;
  due.QuadPart = -us * 10;  // relative, in 100 ns units
  if (SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0)) WaitForSingleObject(timer, INFINITE);
}
#endif

}  // namespace

void Fm4RingWaitBackoff() {
#if defined(_WIN32)
  if (!REXCVAR_GET(fm4_ring_wait_backoff)) return;
  thread_local int64_t wait_start = 0, last_turn = 0;
  const int64_t now = NowUs();
  if (now - last_turn > kNewWaitUs) wait_start = now;  // one loop turn takes well under a microsecond
  if (now - wait_start > kSpinUs) {
    SleepUs(kSleepUs);
    last_turn = NowUs();
    return;
  }
  last_turn = now;
#endif
}
