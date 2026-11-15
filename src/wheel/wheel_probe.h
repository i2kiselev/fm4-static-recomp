// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

namespace fm4_static_recomp::wheel {

// --fm4_wheel_probe: the launcher's WHEEL panel. Writes the connected devices and what the
// game would read from them as one JSON line to stdout about 30 times a second, and reads
// commands from stdin, one per line:
//   set <name> <value>   sets fm4_wheel_<name> for the live view and the next test
//   test                 pulls the force device left, then right
//   testrumble           plays the engine's buzz, then the road's rumble
//   quit                 (or the end of stdin) stops
// Sends no force unless asked, so the game can have the force device.
int RunWheelProbe();

}  // namespace fm4_static_recomp::wheel
