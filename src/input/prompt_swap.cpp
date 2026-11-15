// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "input/prompt_swap.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <string_view>

#include <rex/graphics/pipeline/texture/info.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/hash.h>
#include <rex/logging.h>

#include "input/bc3_encoder.h"
#include "input/keycap.h"

namespace fm4_static_recomp::input {
namespace {

using rex::graphics::texture_replacement::RuntimeImage;
using rex::graphics::xenos::TextureFormat;

// The keycaps are drawn at 4x the icons (the texture cache's larger
// replacement path), with every level down to the icon's size and, for the
// uncompressed ones, further down to 1x1.
constexpr uint32_t kScale = 4;
// The pad's BACK circle ends at x 26 of 32 and the game draws its home icon
// right after it, so the BACK keycaps end there too.
constexpr uint32_t kBackKeycapWidth = 32 * kScale * 27 / 32;
// An .xds is a 52-byte D3D texture header, the last 24 bytes of which are the
// texture fetch constant, then the data as the game loads it.
constexpr size_t kXdsHeaderSize = 0x34;
constexpr size_t kXdsFetchOffset = 0x1C;

constexpr std::string_view kTelemetryExit = "HUD/Icon_D_Pad_UpDown";
constexpr std::string_view kTelemetryTabLeft = "HUD/TEL_TabIcon_L";
constexpr std::string_view kTelemetryTabRight = "HUD/TEL_TabIcon_R";

std::vector<PadControl> ControlsOf(std::string_view name, InputContext context) {
  using enum PadControl;
  if (name == "A_Button_F3")
    return {kA};
  if (name == "B_Button_F3")
    return {kB};
  if (name == "X_Button_F3")
    return {kX};
  if (name == "Y_Button_F3")
    return {kY};
  if (name == "Back_Button_F3" || name == "Back_Button_D_F3")
    return {kBack};
  if (name == "Start_Button_F3")
    return {kStart};
  if (name == "D_Pad_Button_F3")
    return {kDpadUp, kDpadLeft, kDpadDown, kDpadRight};
  if (name == "D_Pad_Left_Right_F3")
    return {kDpadLeft, kDpadRight};
  if (name == "LTrigger_F3")
    return {kLeftTrigger};
  if (name == "RTrigger_F3")
    return {kRightTrigger};
  if (name == "RBumper_F3" || name == "RBumper_I")
    return {kRightShoulder};
  // While driving the left stick only steers.
  if (name == "L_Thumbstick_F3") {
    return context == InputContext::kDriving
               ? std::vector<PadControl>{kLStickLeft, kLStickRight}
               : std::vector<PadControl>{kLStickUp, kLStickLeft, kLStickDown, kLStickRight};
  }
  if (name == "RThumbstick_F3")
    return {kRStickUp, kRStickLeft, kRStickDown, kRStickRight};
  if (name == kTelemetryTabLeft)
    return {kDpadLeft};
  if (name == kTelemetryTabRight)
    return {kDpadRight};
  return {};
}

// The first key of each control, as the keycap shows it.
std::vector<std::string> Labels(const std::vector<PadControl>& controls, const BindingSet& set) {
  std::vector<std::string> labels;
  for (PadControl control : controls) {
    const std::string& bind = set.keys[size_t(control)];
    std::string label = KeyLabel(std::string_view(bind).substr(0, bind.find(',')));
    if (labels.empty() || labels.back() != label)
      labels.push_back(std::move(label));
  }
  return labels;
}

struct Canvas {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> pixels;  // RGBA, straight alpha

  Canvas(uint32_t w, uint32_t h) : width(w), height(h), pixels(size_t(w) * h * 4) {}

  // Draws a colour of this coverage over the texel.
  void Over(uint32_t x, uint32_t y, uint8_t color, float alpha) {
    uint8_t* const p = &pixels[(size_t(y) * width + x) * 4];
    const float below = p[3] / 255.0f;
    const float out = alpha + below * (1.0f - alpha);
    if (out <= 0.0f)
      return;
    for (size_t c = 0; c < 3; ++c)
      p[c] = uint8_t(std::lround((color * alpha + p[c] * below * (1.0f - alpha)) / out));
    p[3] = uint8_t(std::lround(out * 255.0f));
  }
};

// The texture's levels from this image down to the guest icon's size (BC3
// blocks need a multiple of 4), or to 1x1; each a 2x2 average (alpha-weighted).
std::vector<Canvas> Levels(Canvas top, const PromptTexture& texture) {
  std::vector<Canvas> levels{std::move(top)};
  const bool compressed = texture.format == TextureFormat::k_DXT4_5;
  for (;;) {
    const Canvas& above = levels.back();
    if (above.width == 1 || above.height == 1 || (compressed && above.width == texture.width))
      break;
    Canvas level(above.width / 2, above.height / 2);
    for (uint32_t y = 0; y < level.height; ++y) {
      for (uint32_t x = 0; x < level.width; ++x) {
        float color[3] = {}, alpha = 0.0f;
        for (uint32_t dy = 0; dy < 2; ++dy) {
          for (uint32_t dx = 0; dx < 2; ++dx) {
            const uint8_t* p = &above.pixels[((size_t(y) * 2 + dy) * above.width + x * 2 + dx) * 4];
            for (size_t c = 0; c < 3; ++c)
              color[c] += p[c] * (p[3] / 255.0f);
            alpha += p[3] / 255.0f;
          }
        }
        uint8_t* const out = &level.pixels[(size_t(y) * level.width + x) * 4];
        for (size_t c = 0; c < 3; ++c)
          out[c] = alpha > 0.0f ? uint8_t(std::lround(color[c] / alpha)) : 0;
        out[3] = uint8_t(std::lround(alpha / 4.0f * 255.0f));
      }
    }
    levels.push_back(std::move(level));
  }
  return levels;
}

std::shared_ptr<const RuntimeImage> ImageOf(const PromptTexture& texture, Canvas top) {
  auto image = std::make_shared<RuntimeImage>();
  image->format = texture.format;
  image->width = top.width;
  image->height = top.height;
  for (const Canvas& level : Levels(std::move(top), texture)) {
    if (texture.format == TextureFormat::k_DXT4_5) {
      const auto blocks = EncodeBc3(level.pixels, level.width, level.height);
      image->data.insert(image->data.end(), blocks.begin(), blocks.end());
    } else {
      image->data.insert(image->data.end(), level.pixels.begin(), level.pixels.end());
    }
    ++image->levels;
  }
  return image;
}

Canvas KeycapCanvas(const Rgba& cap) {
  Canvas canvas(cap.size, cap.size);
  canvas.pixels = cap.pixels;
  return canvas;
}

// Whether a point of the 44x24 icon, in texels from the chevron's side, is on
// the chevron: a band 5 texels wide (across) between two Vs pointing at x 0,
// cut off where it reaches x 9.
bool OnChevron(float x, float y) {
  constexpr float kTipY = 12.75f, kBand = 5.0f, kCut = 9.0f;
  const float outer = std::abs(y - kTipY);
  return x >= outer && x <= outer + kBand && x <= kCut;
}

// The part of a square of side `extent` texels around a point that is on the
// chevron, 4x4 samples.
float ChevronCover(float x, float y, float extent) {
  int inside = 0;
  for (int sy = 0; sy < 4; ++sy) {
    for (int sx = 0; sx < 4; ++sx)
      inside += OnChevron(x + ((sx + 0.5f) / 4 - 0.5f) * extent,
                          y + ((sy + 0.5f) / 4 - 0.5f) * extent);
  }
  return inside / 16.0f;
}

// The telemetry tab icon: the game's chevron (white, with a soft shadow below
// right) at the outer edge and the keycap where the pad's D-pad was. Measured
// on the 44x24 icon; the left one is the right one mirrored.
Canvas TelemetryTab(const Rgba& cap, bool left) {
  constexpr uint32_t kWidth = 44 * kScale, kHeight = 24 * kScale;
  constexpr float kTexel = 1.0f / kScale;
  constexpr float kShadowX = 1.0f, kShadowY = 0.5f, kShadowAlpha = 0.45f;
  Canvas canvas(kWidth, kHeight);
  for (uint32_t y = 0; y < kHeight; ++y) {
    for (uint32_t x = 0; x < kWidth; ++x) {
      // In icon texels, measured from the chevron's side; the shadow falls to
      // the screen's right either way.
      const float ix = (left ? x + 0.5f : kWidth - x - 0.5f) * kTexel;
      const float iy = (y + 0.5f) * kTexel;
      const float shadow = ChevronCover(ix - (left ? kShadowX : -kShadowX), iy - kShadowY, 1.5f);
      canvas.Over(x, y, 0, shadow * kShadowAlpha);
      canvas.Over(x, y, 255, ChevronCover(ix, iy, kTexel));
    }
  }
  // The D-pad circle filled the icon's height, ending a texel before the far edge.
  const uint32_t cap_x = left ? kWidth - cap.size - kScale : 0;
  for (uint32_t y = 0; y < cap.size; ++y) {
    for (uint32_t x = 0; x < cap.size; ++x) {
      const uint8_t* p = &cap.pixels[(size_t(y) * cap.size + x) * 4];
      canvas.Over(cap_x + x, y, p[0], p[3] / 255.0f);
    }
  }
  return canvas;
}

std::shared_ptr<const RuntimeImage> KeyboardImage(const PromptTexture& texture,
                                                  const hostui::VectorFont& font,
                                                  const Controls& controls, InputContext context) {
  // The telemetry runs while driving; keyboard players exit it with the key the
  // hint names, so its D-pad icon goes.
  if (texture.name == kTelemetryExit)
    return ImageOf(texture, Canvas(texture.width, texture.height));
  if (texture.name == kTelemetryTabLeft || texture.name == kTelemetryTabRight) {
    const auto labels = Labels(ControlsOf(texture.name, context), controls.driving);
    return ImageOf(texture, TelemetryTab(RenderKeycap(labels, texture.height * kScale, font),
                                         texture.name == kTelemetryTabLeft));
  }
  const BindingSet& set = context == InputContext::kDriving ? controls.driving : controls.menu;
  const auto labels = Labels(ControlsOf(texture.name, context), set);
  const bool back = texture.name == "Back_Button_F3" || texture.name == "Back_Button_D_F3";
  const uint32_t size = texture.width * kScale;
  return ImageOf(texture,
                 KeycapCanvas(RenderKeycap(labels, size, font, back ? kBackKeycapWidth : size)));
}

}  // namespace

std::vector<PromptTexture> LoadPromptTextures(const hostui::UiArchive& textures) {
  namespace texture_util = rex::graphics::texture_util;
  std::vector<PromptTexture> out;
  for (const char* name : kPromptTextureNames) {
    const auto data = textures.Read(std::string("UI4/") + name + ".xds");
    if (!data || data->size() <= kXdsHeaderSize) {
      REXLOG_WARN("Prompt icons: cannot read {}", name);
      continue;
    }
    uint32_t dwords[6];
    for (size_t i = 0; i < 6; ++i) {
      const uint8_t* const word = data->data() + kXdsFetchOffset + i * 4;
      dwords[i] = uint32_t(word[0]) << 24 | word[1] << 16 | word[2] << 8 | word[3];
    }
    rex::graphics::xenos::xe_gpu_texture_fetch_t fetch;
    std::memcpy(&fetch, dwords, sizeof(fetch));
    uint32_t width_minus_1, height_minus_1, base_page, mip_page, max_level;
    texture_util::GetSubresourcesFromFetchConstant(fetch, &width_minus_1, &height_minus_1, nullptr,
                                                   &base_page, &mip_page, nullptr, &max_level);
    const TextureFormat format = rex::graphics::GetBaseFormat(fetch.format);
    const auto layout = texture_util::GetGuestTextureLayout(
        fetch.dimension, fetch.pitch, width_minus_1 + 1, height_minus_1 + 1, 1, fetch.tiled,
        format, fetch.packed_mips, true, max_level);
    // The data starts with the base level, which is what the cache hashes.
    const size_t base_bytes = layout.base.level_data_extent_bytes;
    if ((format != TextureFormat::k_8_8_8_8 && format != TextureFormat::k_DXT4_5) ||
        data->size() - kXdsHeaderSize < base_bytes) {
      REXLOG_WARN("Prompt icons: unexpected layout of {}", name);
      continue;
    }
    out.push_back({name, XXH3_64bits(data->data() + kXdsHeaderSize, base_bytes), format,
                   width_minus_1 + 1, height_minus_1 + 1, max_level + 1});
  }
  return out;
}

PromptSwap::PromptSwap(std::vector<PromptTexture> textures, const hostui::VectorFont& font,
                       const Controls& controls, Registry registry)
    : textures_(std::move(textures)),
      registered_(textures_.size()),
      registry_(std::move(registry)) {
  for (InputContext context : {InputContext::kMenu, InputContext::kDriving}) {
    auto& keycaps = keycaps_[size_t(context)];
    for (const PromptTexture& texture : textures_)
      keycaps.push_back(KeyboardImage(texture, font, controls, context));
  }
}

void PromptSwap::Update(bool keyboard_active, InputContext context) {
  for (size_t i = 0; i < textures_.size(); ++i) {
    Image wanted = keyboard_active ? keycaps_[size_t(context)][i] : nullptr;
    if (wanted != registered_[i]) {
      registered_[i] = wanted;
      registry_(textures_[i].hash, std::move(wanted));
    }
  }
}

}  // namespace fm4_static_recomp::input
