// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <rex/graphics/pipeline/texture/fm4_runtime_replacement.h>

namespace fm4_static_recomp::signage {

// Track signage: your own pictures on a track's boards. PC OPTIONS > SIGNAGE lists the tracks, each
// track its boards, and each board takes any PNG from the signage folder (beside the config folder)
// or the game's own. The choices are saved in <config>\signage.toml as "<track>.<board> = <file>"
// and shown live: the folder is watched, so a changed picture shows within seconds too.
//
// A board is one of the track's textures, matched by the hash the SDK's texture replacement uses
// (XXH3 of the texture's base level as the game loads it: the track archive's _0xNNN_B.bix). The
// picture is stretched to the board's shape at 1x, 2x or 4x its size (the largest its resolution
// covers), with every level down to 1x1, encoded as DXT5 like the board, and registered with
// rex::graphics::texture_replacement::SetRuntimeReplacement.

struct Board {
  std::string_view key;   // in signage.toml, after the track's key
  std::string_view name;  // as the SIGNAGE page shows it
  uint32_t texture;       // the track archive's _0xNNNNNNNN_B.bix
  uint64_t hash;
  uint32_t width;
  uint32_t height;
  uint32_t levels;
};

struct Track {
  std::string_view key;      // "hockenheim"
  std::string_view name;     // "HOCKENHEIMRING"
  std::string_view archive;  // its textures, from the game's data folder
  std::span<const Board> boards;
};

std::span<const Track> Tracks();
const Track* FindTrack(std::string_view key);
const Board* FindBoard(const Track& track, std::string_view key);
// "<track>.<board>", as signage.toml names a board.
std::string ChoiceKey(const Track& track, const Board& board);

// A choice per board: ChoiceKey -> the picture's file name in the signage folder.
using Choices = std::map<std::string, std::string>;
// signage.toml's text, and back: a line "<track>.<board> = "<file>"" per choice, "#" comments.
// Reading keeps only boards that exist and plain file names (no folders).
std::string FormatChoices(const Choices& choices);
Choices ParseChoices(std::string_view text);

// An RGBA image (row by row, 4 bytes a pixel).
struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;
};

// 1, 2 or 4: the largest scale whose size the image (w x h) still covers, at least 1.
uint32_t ScaleFor(const Board& board, uint32_t width, uint32_t height);
// Bilinear resampling to dw x dh.
Image Resample(const Image& image, uint32_t dw, uint32_t dh);
// A copy at most `max_width` x `max_height`, keeping its shape (for previews).
Image Fit(const Image& image, uint32_t max_width, uint32_t max_height);
// The board's replacement from a picture: DXT5, at ScaleFor's size, every level down to 1x1.
std::shared_ptr<const rex::graphics::texture_replacement::RuntimeImage> BuildImage(const Board& board,
                                                                                const Image& picture);
// The base level of an Xbox 360 tiled DXT5 texture (as a track archive's _B.bix holds it) as RGBA.
Image DecodeTiledDxt5(std::span<const uint8_t> data, uint32_t width, uint32_t height);

}  // namespace fm4_static_recomp::signage
