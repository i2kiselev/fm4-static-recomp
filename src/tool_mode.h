// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

namespace fm4_static_recomp {

// Launcher helpers that run the executable without starting the game.
enum class ToolMode { kNone, kInstall, kSchema, kShaderCache, kUiFonts, kWheelProbe };

ToolMode CurrentToolMode();

}  // namespace fm4_static_recomp
