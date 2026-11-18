// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <rex/ui/immediate_drawer.h>
#include <rex/ui/presenter.h>
#include <rex/ui/ui_drawer.h>

#include "ui/hostui/glyph_atlas.h"
#include "ui/hostui/ui_archive.h"
#include "ui/hostui/ui_fonts.h"
#include "ui/hostui/vector_font.h"
#include "ui/hostui/xds_texture.h"

namespace fm4_static_recomp::ui {

// QUICK RESTART's screen over the game while the race restarts under it: the game's own
// restart loading screen (its logo backdrop, spinner and text, read from the disc), so the pause
// menu and the pre-race screen the restart goes through for a frame or two are never seen.
// UI thread only.
class QuickRestartScreen final : public rex::ui::UIDrawer {
 public:
  static constexpr size_t kZOrder = 1u << 30;  // over the game and its overlays
  using Clock = std::chrono::steady_clock;

  QuickRestartScreen(rex::ui::Presenter& presenter, rex::ui::ImmediateDrawer& drawer,
           std::filesystem::path game_data_root)
      : presenter_(presenter), drawer_(drawer), game_data_root_(std::move(game_data_root)) {}
  ~QuickRestartScreen() {
    if (registered_) presenter_.RemoveUIDrawerFromUIThread(this);
  }

  // Black, fading in over fade seconds (0: at once); already shown, it stays.
  void Show(double fade) {
    if (!registered_ || hiding_) {
      shown_at_ = Clock::now();
      fade_in_ = fade;
      loading_ = false;
    }
    hiding_ = false;
    Register();
  }

  // The loading screen over the black, fading in over fade seconds: the restart is under way.
  void ShowLoading(double fade) {
    Show(0.0);
    if (!loading_) {
      loading_ = true;
      loading_at_ = Clock::now();
      loading_fade_ = fade;
    }
  }

  // Fades the screen out over fade seconds, after hold seconds.
  void Hide(double hold, double fade) {
    if (!registered_) return;
    hiding_ = true;
    hide_at_ = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(hold));
    fade_ = fade;
  }

  void Draw(rex::ui::UIDrawContext& context) override {
    const auto now = Clock::now();
    float alpha = 1.0f;
    if (fade_in_ > 0) {
      alpha = float(std::min(1.0, std::chrono::duration<double>(now - shown_at_).count() / fade_in_));
    }
    bool background_done = false;
    if (hiding_) {
      const double since = std::chrono::duration<double>(now - hide_at_).count();
      if (since >= fade_) {
        background_done = true;
        alpha = 0.0f;
      } else if (since > 0) {
        alpha = std::min(alpha, float(1.0 - since / fade_));
      }
    }
    if (background_done) {
      registered_ = false;
      hiding_ = false;
      presenter_.RemoveUIDrawerFromUIThread(this);
      return;
    }
    const float w = float(context.render_target_width());
    const float h = float(context.render_target_height());
    drawer_.Begin(context, w, h);
    if (alpha > 0.0f) {
      Quad(White(), 0, 0, w, h, 0, 0, 1, 1, Color(0, 0, 0, alpha));
      if (loading_) {
        const double since = std::chrono::duration<double>(now - loading_at_).count();
        const float in = loading_fade_ > 0 ? float(std::min(1.0, since / loading_fade_)) : 1.0f;
        DrawLoading(w, h, alpha * in, now);
      }
    }
    drawer_.End();
  }

 private:
  void Register() {
    if (!registered_) {
      registered_ = true;
      presenter_.AddUIDrawerFromUIThread(this, kZOrder);
    }
  }

  static uint32_t Color(uint8_t r, uint8_t g, uint8_t b, float alpha) {
    const auto a = uint32_t(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (a << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | r;
  }

  rex::ui::ImmediateTexture* White() {
    if (!white_) {
      const uint8_t white[4] = {255, 255, 255, 255};
      white_ = drawer_.CreateTexture(1, 1, rex::ui::ImmediateTextureFilter::kNearest, false, white);
    }
    return white_.get();
  }

  // A quad, optionally turned by `turns` about its centre.
  void Quad(rex::ui::ImmediateTexture* texture, float x, float y, float width, float height, float u0,
            float v0, float u1, float v1, uint32_t color, float turns = 0.0f) {
    if (!texture) return;
    float px[4] = {x, x + width, x + width, x};
    float py[4] = {y, y, y + height, y + height};
    if (turns != 0.0f) {
      const float cx = x + width * 0.5f, cy = y + height * 0.5f;
      const float c = std::cos(turns * 6.2831853f), s = std::sin(turns * 6.2831853f);
      for (int i = 0; i < 4; ++i) {
        const float dx = px[i] - cx, dy = py[i] - cy;
        px[i] = cx + dx * c - dy * s;
        py[i] = cy + dx * s + dy * c;
      }
    }
    const rex::ui::ImmediateVertex vertices[4] = {{px[0], py[0], u0, v0, color},
                                                  {px[1], py[1], u1, v0, color},
                                                  {px[2], py[2], u1, v1, color},
                                                  {px[3], py[3], u0, v1, color}};
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    rex::ui::ImmediateDrawBatch batch;
    batch.vertices = vertices;
    batch.vertex_count = 4;
    batch.indices = indices;
    batch.index_count = 6;
    drawer_.BeginDrawBatch(batch);
    rex::ui::ImmediateDraw draw;
    draw.count = 6;
    draw.texture = texture;
    drawer_.Draw(draw);
    drawer_.EndDrawBatch();
  }

  std::unique_ptr<rex::ui::ImmediateTexture> LoadTexture(std::string_view member) {
    if (!textures_) textures_ = hostui::UiArchive::Open(game_data_root_ / "Media" / "UI" / "Textures.zip");
    if (!textures_) return nullptr;
    const auto data = textures_->Read(member);
    const auto image = data ? hostui::DecodeXds(*data) : std::nullopt;
    if (!image) return nullptr;
    return drawer_.CreateTexture(image->width, image->height, rex::ui::ImmediateTextureFilter::kLinear,
                                 false, image->rgba.data());
  }

  hostui::GlyphAtlas* Atlas(size_t face, float pixels) {
    auto& font = fonts_[face];
    auto& atlas = atlases_[face];
    auto& texture = atlas_textures_[face];
    if (!font) {
      if (font_failed_) return nullptr;
      const auto archive = hostui::UiArchive::Open(game_data_root_ / "Media" / "UI" / "Fonts.zip");
      const auto data = archive ? archive->Read(hostui::kUiFontMembers[face]) : std::nullopt;
      auto parsed = data ? hostui::VectorFont::Parse(*data) : std::nullopt;
      if (!parsed) {
        font_failed_ = true;
        return nullptr;
      }
      font = std::make_unique<hostui::VectorFont>(std::move(*parsed));
    }
    if (!atlas || atlas->pixels_per_em() != pixels) {
      atlas = std::make_unique<hostui::GlyphAtlas>(*font, pixels);
      texture.reset();
    }
    return atlas.get();
  }

  // `text` centred at (cx, baseline) in one of the UI's two font faces.
  void Text(size_t face, float pixels, std::u32string_view text, float cx, float baseline, uint32_t color) {
    hostui::GlyphAtlas* const atlas = Atlas(face, pixels);
    if (!atlas) return;
    auto& texture = atlas_textures_[face];
    if (atlas->Prepare(text) || !texture) {
      texture = drawer_.CreateTexture(atlas->width(), atlas->height(), rex::ui::ImmediateTextureFilter::kLinear,
                                      false, atlas->rgba().data());
      if (!texture) return;
    }
    float pen = std::round(cx - atlas->Measure(text) * 0.5f);
    const float iw = 1.0f / float(atlas->width()), ih = 1.0f / float(atlas->height());
    for (const char32_t code_point : text) {
      const hostui::GlyphAtlas::Entry* entry = atlas->Find(code_point);
      if (!entry) continue;
      if (entry->width && entry->height) {
        Quad(texture.get(), pen + float(entry->left), std::round(baseline) + float(entry->top),
             float(entry->width), float(entry->height), float(entry->x) * iw, float(entry->y) * ih,
             float(entry->x + entry->width) * iw, float(entry->y + entry->height) * ih, color);
      }
      pen += entry->advance;
    }
  }

  // The game's restart loading screen: its 1280x720 logo backdrop, fitted to the height and
  // centred, the spinner ring 70% down turning an eighth every 90 ms, and LOADING... PLEASE WAIT.
  void DrawLoading(float w, float h, float alpha, Clock::time_point now) {
    if (!loading_tried_) {
      loading_tried_ = true;
      backdrop_ = LoadTexture("UI4/Common/Forza_Text.xds");
      spinner_ = LoadTexture("UI4/Common/LOADING_SPINNER_RGB.xds");
    }
    const float scale = h / 720.0f;
    const float bw = 1280.0f * scale;
    if (backdrop_) Quad(backdrop_.get(), (w - bw) * 0.5f, 0, bw, h, 0, 0, 1, 1, Color(255, 255, 255, alpha));
    const double spin = std::chrono::duration<double>(now - shown_at_).count();
    const float step = float(std::floor(spin / 0.09)) / 8.0f;
    const float size = 50.0f * scale;
    if (spinner_) {
      Quad(spinner_.get(), w * 0.5f - size * 0.5f, h * 0.706f - size * 0.5f, size, size, 0, 0, 1, 1,
           Color(255, 255, 255, alpha), step);
    }
    Text(1, 14.0f * scale, U"LOADING... PLEASE WAIT", w * 0.5f, h * 0.912f, Color(150, 150, 150, alpha));
  }

  rex::ui::Presenter& presenter_;
  rex::ui::ImmediateDrawer& drawer_;
  std::filesystem::path game_data_root_;
  std::unique_ptr<rex::ui::ImmediateTexture> white_;
  std::optional<hostui::UiArchive> textures_;
  bool loading_tried_ = false;
  std::unique_ptr<rex::ui::ImmediateTexture> backdrop_;
  std::unique_ptr<rex::ui::ImmediateTexture> spinner_;
  std::unique_ptr<hostui::VectorFont> fonts_[2];
  std::unique_ptr<hostui::GlyphAtlas> atlases_[2];
  std::unique_ptr<rex::ui::ImmediateTexture> atlas_textures_[2];
  bool font_failed_ = false;
  bool registered_ = false;
  bool hiding_ = false;
  Clock::time_point hide_at_{};
  Clock::time_point shown_at_{};
  double fade_in_ = 0.0;
  bool loading_ = false;
  Clock::time_point loading_at_{};
  double loading_fade_ = 0.25;
  double fade_ = 0.3;
};

}  // namespace fm4_static_recomp::ui
