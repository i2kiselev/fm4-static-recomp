// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rex/graphics/pipeline/texture/fm4_runtime_replacement.h>

#include "track_signage.h"

// Sharper track textures (PC OPTIONS > MODS > TRACK CUSTOMISATION): a track's own textures upscaled 2x or 4x
// from the player's disc, the first time a size is chosen, and shown live.
//
// TRACK TEXTURES (fm4_track_textures) covers the textures worth it (not normal maps, flat colours or masks);
// ROAD SURFACE (fm4_road_surface) and GRASS (fm4_grass_texture) set those two on their own, over it. Each is
// default, 2x or 4x. A picture of the player's own on the same texture (TRACK SIGNAGE) is shown instead.
//
// Upscaling: Real-ESRGAN (realesrgan-ncnn-vulkan, its x4plus model), downloaded once at a pinned version and
// checked against its SHA-256, run on the graphics card in the background. Each texture is padded with its own
// wrapped edges first, so it stays seamless when tiled; the result keeps the original's colours and alpha. 2x is
// the 4x result halved. The results are kept (state\track-textures), so a size is upscaled only once.
namespace fm4_static_recomp::track_textures {

enum class Scale : uint32_t { kDefault = 1, k2x = 2, k4x = 4 };
std::optional<Scale> ParseScale(std::string_view value);  // "default", "2x", "4x"
std::string_view ScaleName(Scale scale);                  // as the settings take it
std::string ScaleLabel(Scale scale);                      // DEFAULT, 2X, 4X

enum class Row { kTrack, kRoad, kGrass };

// Hockenheim's road and grass (each one texture, tiled over the whole circuit).
inline constexpr uint64_t kRoadHash = 0x5E1088ECE2F85B61ull;
inline constexpr uint64_t kGrassHash = 0xBB1899D1710E3C0Eull;

// The pure parts (tested on their own).
// Whether TRACK TEXTURES upscales this texture: some fine detail, not a tangent-space normal map, not mostly
// white (masks).
bool WorthUpscaling(const signage::Image& image);
// The picture with `border` pixels of its own wrapped edges around it, and back.
signage::Image WrapPad(const signage::Image& image, uint32_t border);
signage::Image Crop(const signage::Image& image, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
// Each colour channel of `image` given the distribution of `reference`'s (alpha left alone).
void MatchColours(signage::Image& image, const signage::Image& reference);
// RGBA as a PNG (stored, no compression: written only for the upscaler to read).
std::vector<uint8_t> EncodePng(const signage::Image& image);
// The upscaled replacement of `original` from the upscaler's result of WrapPad(original, border) at 4x: the
// centre cropped, the original's colours and alpha, halved for 2x. `detail` mixes the upscaler's detail with the
// original's, smoothly enlarged: 1 all of it, 0.5 half (a small texture tiled far, as the road, shows its repeats
// and shimmers with all of it).
std::shared_ptr<const rex::graphics::texture_replacement::RuntimeImage> Finish(const signage::Image& original,
                                                                              const signage::Image& upscaled,
                                                                              uint32_t border, Scale scale, bool dxt1,
                                                                              float detail = 1.0f);
// How much of the upscaler's detail a texture keeps (Finish).
float DetailFor(uint64_t hash);
// A replacement kept on disk, and read back.
std::vector<uint8_t> SerializeImage(const rex::graphics::texture_replacement::RuntimeImage& image);
std::shared_ptr<const rex::graphics::texture_replacement::RuntimeImage> DeserializeImage(std::span<const uint8_t> data);

// The three settings, kept in their own file (config\track-textures.toml) whichever page sets them.
struct Settings {
  Scale track = Scale::kDefault, road = Scale::kDefault, grass = Scale::kDefault;
  bool operator==(const Settings&) const = default;
};
std::string FormatSettings(const Settings& settings);  // a line per setting not at default
Settings ParseSettings(std::string_view text);         // unknown lines and values ignored
// As set now (a launch option included).
Settings CurrentSettings();
// Writes the file and sets them at once (a setting a launch option gave stays as given this session);
// false when the file could not be written.
bool SaveSettings(const Settings& settings);
// Where SaveSettings writes, without starting the worker (the pages' tests).
void SetSettingsFile(const std::filesystem::path& file);

// The running side. `state_root` holds the results and the upscaler; `game_root` Media\Tracks; `settings_file`
// the three settings, read now.
void Start(const std::filesystem::path& state_root, const std::filesystem::path& game_root,
           const std::filesystem::path& settings_file);
// The settings changed (SAVE & APPLY): shows what is made, and upscales what is missing.
void Apply();
void Stop();
// The row's note, as now: upscaling progress, done, or what the first choice will do.
std::string Note(Row row);
// What this feature shows on a texture now (TRACK SIGNAGE gives it back when it lets a texture go), or nullptr.
std::shared_ptr<const rex::graphics::texture_replacement::RuntimeImage> ImageFor(uint64_t hash);

}  // namespace fm4_static_recomp::track_textures
