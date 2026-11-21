#include <catch2/catch_test_macros.hpp>

#include "fence_wait.h"

using fm4_static_recomp::FenceWaitBackoff;

TEST_CASE("A fence wait spins for its first 50 us, then sleeps", "[fence_wait]") {
  FenceWaitBackoff backoff;
  CHECK_FALSE(backoff.ShouldSleep(1000));
  CHECK_FALSE(backoff.ShouldSleep(1010));
  CHECK_FALSE(backoff.ShouldSleep(1030));
  CHECK_FALSE(backoff.ShouldSleep(1050));
  CHECK(backoff.ShouldSleep(1060));
}

TEST_CASE("A fence wait keeps sleeping across its steps", "[fence_wait]") {
  FenceWaitBackoff backoff;
  CHECK_FALSE(backoff.ShouldSleep(0));
  for (int64_t pass = 10; pass <= 60; pass += 10) backoff.ShouldSleep(pass);
  backoff.Slept(165);
  CHECK(backoff.ShouldSleep(166));
  backoff.Slept(270);
  CHECK(backoff.ShouldSleep(271));
}

TEST_CASE("A gap between passes starts a new wait that spins again", "[fence_wait]") {
  FenceWaitBackoff backoff;
  for (int64_t pass = 0; pass <= 60; pass += 10) backoff.ShouldSleep(pass);
  backoff.Slept(165);
  CHECK_FALSE(backoff.ShouldSleep(5000));
  for (int64_t pass = 5015; pass <= 5045; pass += 15) CHECK_FALSE(backoff.ShouldSleep(pass));
  CHECK(backoff.ShouldSleep(5060));
}

TEST_CASE("The first pass of the first wait never sleeps", "[fence_wait]") {
  FenceWaitBackoff backoff;
  CHECK_FALSE(backoff.ShouldSleep(0));
  FenceWaitBackoff late;
  CHECK_FALSE(late.ShouldSleep(1'000'000'000'000));
}
