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

#ifndef FM4_HAVE_NEURAL_RENDERING
#if defined(__has_include)
#if __has_include(<rex/ui/vulkan/vulkan_neural_rendering.h>)
#define FM4_HAVE_NEURAL_RENDERING 1
#else
#define FM4_HAVE_NEURAL_RENDERING 0
#endif
#else
#define FM4_HAVE_NEURAL_RENDERING 0
#endif
#endif

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
  // The refresh rate of the display showing the window when the page opened
  // (0 when unknown).
  std::function<uint32_t()> display_refresh_hz;
  // The draw resolution scale the renderer uses (0 when unknown).
  std::function<uint32_t()> draw_resolution_scale;
  // Where the latest frame reached the window, in pixels.
  std::function<std::optional<std::pair<uint32_t, uint32_t>>()> output_size;
  // The presenter's upscalers, each with whether it runs on this GPU.
  std::function<std::vector<rex::ui::UpscalerInfo>()> upscalers;
  // Whether the SDK Neural Rendering bridge and user-provided runtime are
  // installed. Empty uses the SDK's filesystem check.
  std::function<bool()> neural_rendering_installed;
};

std::unique_ptr<hostui::MenuScreen> CreateGraphicsPage(config::HostConfig& config,
                                                       GraphicsPageServices services);

}  // namespace fm4_static_recomp::ui
