// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "built_in_defaults.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(fm4_config_root, "", "Forza Motorsport 4",
                      "Folder of graphics.toml and controls.toml; empty means the state root's "
                      "config folder");

REXCVAR_DEFINE_BOOL(fm4_show_fps, false, "Forza Motorsport 4",
                    "Show the average guest-output FPS debug overlay on start");

REXCVAR_DEFINE_BOOL(controller_vibration, true, "Input",
                    "Enable controller vibration rumble");

namespace fm4_static_recomp {
namespace {

using Default = std::pair<std::string_view, std::string_view>;

constexpr uint32_t kIntelVendorId = 0x8086;

constexpr std::array kBeforeGpuPlugin = {Default{"gpu_backend", "vulkan"}};

// clear_memory_page_state: the SDK default re-uploads every CPU-written page each frame.
// texture_cache_memory_limit_soft: a race needs about 300 MB at 2x; 256 saved 300 MB.
// async_shader_compilation: a draw whose pipeline is still compiling is dropped, and FM4
// keeps some render targets across frames, so a cold cache darkened the asphalt for seconds.
constexpr std::array kAfterGpuPlugin = {
    Default{"async_shader_compilation", "false"},
    Default{"window_aspect_ratio", "16:9"},
    // Keep CPU pages cached; write-watch invalidation handles changed data.
    Default{"clear_memory_page_state", "false"},
    Default{"host_present_fps_limit", "0"},
    Default{"gpu_record_thread", "true"},
    Default{"texture_cache_memory_limit_soft", "256"},
    Default{"fh1_async_readbacks", "true"},
    // The SDK's changes since pinyon-shift-v0.3.0, held at the behaviour FM4 was verified
    // with until each is measured here. The SDK's own default otherwise.
    Default{"fh1_msaa_single_sample", "false"},
#if defined(__APPLE__)
    // Overlay repaint ticks must not submit the same guest image repeatedly.
    // MoltenVK may block in nextDrawable inside vkQueueSubmit while holding
    // the graphics queue, starving the game's submission worker.
    Default{"pinyon_shift_fh1_source_presentation", "true"},
    // Avoid materializing a clear as a separate render pass when the following
    // draw can consume it as an attachment load action. MoltenVK pays for each
    // Metal encoder; FM4's repeated clears otherwise dominate submission time.
    Default{"fh1_fold_clears", "true"},
    // Preserve query and memexport draws; the SDK only skips draws with no
    // attachment writes, query samples or exported memory.
    Default{"fh1_skip_draws_without_effect", "true"},
#else
    Default{"fh1_fold_clears", "false"},
    Default{"fh1_skip_draws_without_effect", "false"},
#endif
    Default{"fh1_resolve_to_textures", "false"},
    Default{"fh1_direct_resolve_outdated", "false"},
    Default{"fh1_stable_alpha_to_coverage", "false"},
    Default{"fh1_native_stencil_export", "false"},
    Default{"fh1_async_specialized_passes", "false"},
    Default{"texture_band_reloads", "false"},
    Default{"texture_layer_reloads", "false"},
    Default{"texture_selective_binding_reset", "false"},
    Default{"texture_cache_memory_budget_limits", "false"},
    Default{"vulkan_texture_load_compute_copy", "false"},
    Default{"vulkan_small_texture_copy_views", "false"},
#if defined(__APPLE__)
    // The SDK enables FH1's single-pass tiling optimization on macOS. FM4's
    // 4x MSAA menus need the original tile passes or parts of the UI are
    // clipped and replaced by stale EDRAM contents. Match the Windows path
    // while preserving the console's showroom and race MSAA settings.
    Default{"fh1_untile_predicated_tiling", "false"},
    Default{"fh1_readback_small_float_resolves", "true"},
    Default{"vulkan_present_mode", "fifo"},
    Default{"swap_post_effect_scene_only", "false"},
    Default{"execute_unclipped_draw_vs_on_cpu", "false"},
    Default{"force_convert_quad_lists_to_triangle_lists", "true"},
#else
    Default{"fh1_readback_small_float_resolves", "false"},
    // With vsync on the SDK presents mailbox or FIFO, which Nvidia composes through the
    // desktop: no independent flip, so no variable refresh rate, and mailbox dropped from
    // 80 to 50 fps after an alt-tab.
    Default{"vulkan_present_mode", "immediate"},
    Default{"swap_post_effect_scene_only", "false"},
    Default{"execute_unclipped_draw_vs_on_cpu", "false"},
    Default{"force_convert_quad_lists_to_triangle_lists", "false"},
#endif
    Default{"wait_reg_mem_yield_us", "2000"},
    Default{"gpu_d3d_shader_cache", "0"},
    Default{"guest_reservation_sections_unlocked", "false"},
    Default{"rtl_critical_section_pcr_thread", "false"},
    Default{"xma_direct_decode", "false"},
    Default{"xma_lazy_decoder_state", "false"},
    // Decoding on the XMA decoder thread lets the game's mixer now and then read a block before
    // it is decoded: one voice drops out for 5 ms, heard as crackle (#37).
    Default{"xma_decode_kicks_inline", "true"},
    // A new car's garage thumbnail now and then lost a tile-shaped part, with no cause found; none
    // broke with this on. It costs frame drops while a thumbnail is made.
    Default{"fh1_hold_resolve_sync", "true"},
    Default{"pad_chord_debug_overlay", ""},
    // The keyboard plays beside the pad, Left Shift as the clutch while driving keys are
    // held; no mouse input (the game's menus have no pointer).
    Default{"mnk_mode", "true"},
    Default{"mnk_exact_modifiers", "false"},
    Default{"mnk_mouse", "false"},
    Default{"mnk_mouse_steering", "false"},
};

template <size_t N>
void Apply(const std::array<Default, N>& defaults) {
  for (const auto& [name, value] : defaults) {
    if (rex::cvar::GetFlagSource(name) != rex::cvar::Source::kDefault) {
      continue;
    }
    if (rex::cvar::SetFlagByName(name, value)) {
      REXLOG_INFO("Built-in default: {} = {}", name, value);
    } else {
      REXLOG_ERROR("Built-in default: {} refused {}", name, value);
    }
  }
}

}  // namespace

void ApplyBuiltInDefaults(DefaultStage stage) {
  if (stage == DefaultStage::kBeforeGpuPlugin) {
    Apply(kBeforeGpuPlugin);
  } else {
    Apply(kAfterGpuPlugin);
  }
}

// Intel GPUs flicker the scene's lights with the generic resolve and EDRAM transfer passes.
void ApplyGpuVendorDefaults(uint32_t vendor_id) {
  Apply(std::array{
      Default{"fh1_specialize_edram_passes", vendor_id == kIntelVendorId ? "true" : "false"}});
}

}  // namespace fm4_static_recomp
