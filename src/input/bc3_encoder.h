// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace fm4_static_recomp::input {

// BC3 (DXT5) blocks, row by row, of a straight-alpha RGBA image whose width
// and height are multiples of 4. Endpoints are each block's colour and alpha
// range: enough for the flat greys of keycaps.
std::vector<uint8_t> EncodeBc3(std::span<const uint8_t> rgba, uint32_t width, uint32_t height);

}  // namespace fm4_static_recomp::input
