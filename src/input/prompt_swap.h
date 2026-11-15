// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rex/graphics/pipeline/texture/fm4_runtime_replacement.h>

#include "input/controls.h"
#include "input/input_context.h"
#include "ui/hostui/ui_archive.h"
#include "ui/hostui/vector_font.h"

namespace fm4_static_recomp::input {

// The game's button prompt icons (Textures.zip, UI4/<name>.xds), then the
// telemetry's: the D-pad beside its exit hint and its page tabs' D-pad icons.
inline constexpr std::array<const char*, 18> kPromptTextureNames = {
    "A_Button_F3",         "B_Button_F3",      "X_Button_F3",           "Y_Button_F3",
    "Back_Button_F3",      "Back_Button_D_F3", "Start_Button_F3",       "D_Pad_Button_F3",
    "D_Pad_Left_Right_F3", "LTrigger_F3",      "RTrigger_F3",           "RBumper_F3",
    "RBumper_I",           "L_Thumbstick_F3",  "RThumbstick_F3",        "HUD/Icon_D_Pad_UpDown",
    "HUD/TEL_TabIcon_L",   "HUD/TEL_TabIcon_R"};

struct PromptTexture {
  std::string name;
  uint64_t hash;  // of the guest base level, as the texture cache hashes it
  rex::graphics::xenos::TextureFormat format = rex::graphics::xenos::TextureFormat::k_8_8_8_8;
  uint32_t width = 32, height = 32, levels = 1;
};

// The icons whose .xds the archive holds, hashed, with their guest format and size.
std::vector<PromptTexture> LoadPromptTextures(const hostui::UiArchive& textures);

// Replaces the prompt icons with keycaps of the current context's keys while
// the keyboard was the last device used, and gives the icons back when a pad
// is. The keycaps of both contexts are drawn up front, so a swap only
// registers images.
class PromptSwap {
 public:
  using Registry = std::function<void(
      uint64_t, std::shared_ptr<const rex::graphics::texture_replacement::RuntimeImage>)>;

  PromptSwap(std::vector<PromptTexture> textures, const hostui::VectorFont& font,
             const Controls& controls,
             Registry registry = rex::graphics::texture_replacement::SetRuntimeReplacement);
  void Update(bool keyboard_active, InputContext context);

 private:
  using Image = std::shared_ptr<const rex::graphics::texture_replacement::RuntimeImage>;

  std::vector<PromptTexture> textures_;
  // Per context, per texture.
  std::array<std::vector<Image>, 2> keycaps_;
  std::vector<Image> registered_;
  Registry registry_;
};

}  // namespace fm4_static_recomp::input
