// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <rex/graphics/pipeline/texture/fm4_runtime_replacement.h>

namespace fm4_static_recomp::signage {

// Track signage: a PNG in the signage folder named after a trackside board (README.txt there lists
// them) is shown on that board in races, live. PC OPTIONS > GAMEPLAY > TRACK SIGNAGE turns it off
// and on (fm4_track_signage, live) and opens the folder.
//
// A board is a texture of the track, matched by the hash the SDK's texture replacement uses
// (rex::graphics::texture_replacement::SetRuntimeReplacement). The image is stretched to the
// board's shape at 1x, 2x or 4x its size (the largest the image's resolution covers), with every
// level down to 1x1, and encoded as DXT5 like the board. Its alpha is kept: where a board's alpha
// is a mask (the Santander wall: shine), transparent parts take it away.

struct Board {
  std::string_view file;  // the PNG's name without ".png"
  uint64_t hash;
  uint32_t width;
  uint32_t height;
  uint32_t levels;
  std::string_view what;  // as README.txt describes it
};

std::span<const Board> Boards();
// The board a file stem names (ASCII case ignored), or nullptr.
const Board* FindBoard(std::string_view stem);
// 1, 2 or 4: the largest scale whose size the image (w x h) still covers, at least 1.
uint32_t ScaleFor(const Board& board, uint32_t width, uint32_t height);
// Bilinear resampling of RGBA pixels to dw x dh.
std::vector<uint8_t> Resample(std::span<const uint8_t> rgba, uint32_t width, uint32_t height, uint32_t dw,
                              uint32_t dh);
// The board's replacement from an RGBA image: DXT5, at ScaleFor's size, every level to 1x1.
std::shared_ptr<const rex::graphics::texture_replacement::RuntimeImage> BuildImage(
    const Board& board, std::span<const uint8_t> rgba, uint32_t width, uint32_t height);
// The folder's README.txt: what to name the files, and each board's size.
std::string Readme();

// At startup: makes the folder (with README.txt) and watches it from a thread, every two seconds:
// a new or changed PNG is shown on its board, a removed one gives the board back to the game.
void Start(const std::filesystem::path& folder);
// The boards showing a PNG now.
size_t ShownCount();
// The folder Start watches (empty before it).
std::filesystem::path Folder();
// Opens the folder in the system's file browser (Explorer on Windows).
bool OpenFolder();
// The watcher keeps ShownCount up to date.
void SetShownCount(size_t count);
// Start records the folder and how to open it (the system's file browser).
void SetFolder(const std::filesystem::path& folder, std::function<bool(const std::filesystem::path&)> opener);

}  // namespace fm4_static_recomp::signage
