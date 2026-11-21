// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ui/hostui/vector_font.h"

namespace fm4_static_recomp::input {

// Labels of the arrow keys, drawn as triangles (the game's fonts have no arrows).
inline constexpr std::string_view kArrowUp = "\xE2\x86\x91";
inline constexpr std::string_view kArrowDown = "\xE2\x86\x93";
inline constexpr std::string_view kArrowLeft = "\xE2\x86\x90";
inline constexpr std::string_view kArrowRight = "\xE2\x86\x92";

// What a keycap shows for one token of a keyboard driver bind ("Return",
// "Numpad7", "Shift+W").
std::string KeyLabel(std::string_view bind_token);

struct Rgba {
  uint32_t size = 0;
  std::vector<uint8_t> pixels;  // size x size, RGBA, straight alpha, rows top down
};

// A light keycap: a rounded grey face over a darker bottom edge, the labels
// centred in dark text, shrunk to fit. Several labels (a stick's or the
// D-pad's keys) share the cap; when they would not read, the first one alone.
// A cap_width below size draws the cap that wide from the left, clear beyond.
Rgba RenderKeycap(std::span<const std::string> labels, uint32_t size,
                  const hostui::VectorFont& font, uint32_t cap_width = 0);

// A mouse in the keycaps' colours, its left or right button dark: the prompt for a click (#76).
Rgba RenderMouseButton(bool right, uint32_t size);

}  // namespace fm4_static_recomp::input
