// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <rex/ui/upscaler.h>

#include "config/host_config.h"
#include "ui/hostui/menu.h"

namespace fm4_static_recomp::ui {

// The GRAPHICS page of My Profile -> Options: the race frame rate (a slider),
// preset, resolution scale, upscaler, post-process anti-aliasing, the game's
// MSAA, anisotropic filtering and more. Changes are drafted until SAVE & APPLY
// (Y) saves them to `<state>/config/graphics.toml`, which holds only these
// settings, and applies them one at a time.

// Applies the file's values, skipping settings given on the command line and
// values the page does not offer. Before the GPU is created.
void ApplySavedGraphicsSettings(const config::HostConfig& config);

// The rows, choices, defaults and presets as JSON for the launcher (schema
// version 1). False when the file cannot be written.
bool WriteGraphicsSchema(const std::filesystem::path& path);

// The refresh rate of the display showing `native_window` (an HWND), or 0.
uint32_t DisplayRefreshHz(void* native_window);

// Lowers a saved frame rate above the display's refresh rate (0 = unknown) to
// it, for this session only.
void LimitFrameRateToDisplay(uint32_t refresh_hz);

struct GraphicsPageServices {
  // Whether the window is currently in fullscreen mode.
  std::function<bool()> is_fullscreen;
  // Request entering or leaving fullscreen mode.
  std::function<void(bool)> set_fullscreen;
  // The refresh rate of the display showing the window when the page opened
  // (0 when unknown).
  std::function<uint32_t()> display_refresh_hz;
  // The draw resolution scale the renderer uses (0 when unknown).
  std::function<uint32_t()> draw_resolution_scale;
  // Where the latest frame reached the window, in pixels.
  std::function<std::optional<std::pair<uint32_t, uint32_t>>()> output_size;
  // The presenter's upscalers, each with whether it runs on this GPU.
  std::function<std::vector<rex::ui::UpscalerInfo>()> upscalers;
};

std::unique_ptr<hostui::MenuScreen> CreateGraphicsPage(config::HostConfig& config,
                                                       GraphicsPageServices services);

}  // namespace fm4_static_recomp::ui
