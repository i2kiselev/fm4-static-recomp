// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <vector>

namespace fm4_static_recomp::input {

// The mouse pointer the game window shows (#76): a slim chevron in the game's grey with its red edge and a soft
// shadow (the pointer in Eggtooth's mouse mockups in #76 started the idea). Drawn here, no image file.
struct PointerImage {
  int size = 0;                 // square, pixels
  int hot_x = 0, hot_y = 0;     // the arrow's tip
  std::vector<uint32_t> argb;   // size * size, row by row, straight (not premultiplied) alpha
};

// `size` pixels square (40 at 1080p; more on bigger screens), anti-aliased.
PointerImage DrawPointer(int size);

// The pointer's size for a window this many pixels tall: 40 at 1080, in steps of 8.
int PointerSize(int window_height);

// Makes it the window's mouse pointer wherever the window shows one (`native_window`: its HWND; Windows only).
// False when it cannot (the system's arrow stays).
bool InstallPointerCursor(void* native_window, int window_height);

}  // namespace fm4_static_recomp::input
