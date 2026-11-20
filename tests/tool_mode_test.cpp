// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <rex/cvar.h>

#include "tool_mode.h"

namespace {

using fm4_static_recomp::CurrentToolMode;
using fm4_static_recomp::ToolMode;

struct ResetToolCvars {
  ResetToolCvars() { Reset(); }
  ~ResetToolCvars() { Reset(); }
  static void Reset() {
    rex::cvar::SetFlagByName("fm4_wheel_probe", "false");
    rex::cvar::SetFlagByName("fm4_write_wheel_schema", "");
  }
};

}  // namespace

TEST_CASE("The wheel tool modes are chosen by their cvars", "[tool]") {
  ResetToolCvars reset;
  CHECK(CurrentToolMode() == ToolMode::kNone);

  SECTION("probe") {
    rex::cvar::SetFlagByName("fm4_wheel_probe", "true");
    CHECK(CurrentToolMode() == ToolMode::kWheelProbe);
  }
  SECTION("schema") {
    rex::cvar::SetFlagByName("fm4_write_wheel_schema", "x.json");
    CHECK(CurrentToolMode() == ToolMode::kWheelSchema);
  }
}
