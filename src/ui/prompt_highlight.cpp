// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "ui/prompt_highlight.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <utility>
#include <vector>

namespace fm4_static_recomp::ui {

namespace {

constexpr float kUiWidth = 1280, kUiHeight = 720;
constexpr uint32_t Rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a) { return r | g << 8 | b << 16 | a << 24; }
constexpr uint32_t kRed = Rgba(212, 46, 25, 0);  // the game's red (its menus' highlight line), alpha added
constexpr float kLineAlpha = 235, kGlowAlpha = 58;
constexpr float kLineHeight = 1.25f;  // UI units
constexpr float kLineAbove = 7;       // UI units from the prompt box's bottom up to the line
constexpr float kGlowUp = 5, kGlowDown = 2.5f;  // UI units the glow reaches above and below the line
constexpr float kEndFade = 20, kEndFadeShare = 0.4f;  // the taper at each end: units, at most this share of the width
constexpr int kEndSteps = 8, kGlowSteps = 5;
constexpr float kTileTopAlpha = 74, kTileBottomAlpha = 30;  // the veil on a hovered tile, top to bottom
constexpr double kOpenSeconds = 0.09;  // the line opens from its centre, easing out
constexpr float kOpenFrom = 0.55f;     // of its width

float Smooth(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

}  // namespace

PromptHighlight::PromptHighlight(rex::ui::Presenter& presenter, rex::ui::ImmediateDrawer& drawer)
    : presenter_(presenter), drawer_(drawer) {
  const uint8_t white[4] = {255, 255, 255, 255};
  white_ = drawer_.CreateTexture(1, 1, rex::ui::ImmediateTextureFilter::kNearest, false, white);
}

PromptHighlight::~PromptHighlight() {
  if (registered_) presenter_.RemoveUIDrawerFromUIThread(this);
}

void PromptHighlight::Show(std::optional<input::UiRect> box) {
  if (box && box_ != box) shown_at_ = std::chrono::steady_clock::now();
  box_ = box;
  if (box_ && !registered_) {
    presenter_.AddUIDrawerFromUIThread(this, kZOrder);
    registered_ = true;
  } else if (!box_ && registered_) {
    presenter_.RemoveUIDrawerFromUIThread(this);
    registered_ = false;
  }
  presenter_.RequestUIPaintFromUIThread();
}

void PromptHighlight::Draw(rex::ui::UIDrawContext& context) {
  if (!box_ || !white_) return;
  // The game's 16:9 UI space fitted into the painted picture, as the PC OPTIONS pages place it.
  float x = 0, y = 0, width = float(context.render_target_width()), height = float(context.render_target_height());
  if (const auto rect = presenter_.GetPaintedGuestOutputRectFromUIThread()) {
    x = float(rect->x), y = float(rect->y), width = float(rect->width), height = float(rect->height);
  }
  const float scale = std::min(width / kUiWidth, height / kUiHeight);
  if (scale <= 0) return;
  const float left = x + (width - kUiWidth * scale) / 2, top = y + (height - kUiHeight * scale) / 2;
  const auto to_x = [&](float ux) { return left + (ux + kUiWidth / 2) * scale; };
  const auto to_y = [&](float uy) { return top + (kUiHeight / 2 - uy) * scale; };
  // Fading in: drawn again each paint until it is full.
  const double since = std::chrono::duration<double>(std::chrono::steady_clock::now() - shown_at_).count();
  const float open = float(std::clamp(since / kOpenSeconds, 0.0, 1.0));
  if (open < 1) presenter_.RequestUIPaintFromUIThread();
  const float fade = std::min(1.0f, open * 2.5f);
  const float opened = 1 - (1 - open) * (1 - open) * (1 - open);
  const auto alpha = [&](uint32_t a) { return uint32_t(std::lround(a * fade)); };
  const auto submit = [&](const std::vector<rex::ui::ImmediateVertex>& vertices, const std::vector<uint16_t>& indices) {
    drawer_.Begin(context, float(context.render_target_width()), float(context.render_target_height()));
    rex::ui::ImmediateDrawBatch batch;
    batch.vertices = vertices.data();
    batch.vertex_count = int(vertices.size());
    batch.indices = indices.data();
    batch.index_count = int(indices.size());
    drawer_.BeginDrawBatch(batch);
    rex::ui::ImmediateDraw draw;
    draw.count = int(indices.size());
    draw.texture = white_.get();
    drawer_.Draw(draw);
    drawer_.EndDrawBatch();
    drawer_.End();
  };
  if (box_->tile) {
    const float tx0 = to_x(box_->min_x), tx1 = to_x(box_->max_x), ty0 = to_y(box_->max_y), ty1 = to_y(box_->min_y);
    const uint32_t top = Rgba(255, 255, 255, alpha(uint32_t(kTileTopAlpha)));
    const uint32_t bottom = Rgba(255, 255, 255, alpha(uint32_t(kTileBottomAlpha)));
    submit({{tx0, ty0, 0, 0, top}, {tx1, ty0, 1, 0, top}, {tx1, ty1, 1, 1, bottom}, {tx0, ty1, 0, 1, bottom}},
           {0, 1, 2, 0, 2, 3});
    return;
  }
  const float middle = to_x((box_->min_x + box_->max_x) / 2);
  const float half = (to_x(box_->max_x) - to_x(box_->min_x)) / 2 * (kOpenFrom + (1 - kOpenFrom) * opened);
  const float x0 = middle - half, x1 = middle + half;
  const float line_y = to_y(box_->min_y + kLineAbove), line = std::max(1.0f, kLineHeight * scale);
  const float end = std::min(kEndFade * scale, (x1 - x0) * kEndFadeShare);

  // Columns: a smooth taper at each end, flat between.
  std::vector<std::pair<float, float>> columns;  // x, strength
  for (int i = 0; i <= kEndSteps; ++i) {
    const float t = float(i) / kEndSteps;
    columns.push_back({x0 + end * t, Smooth(t)});
  }
  for (int i = kEndSteps; i >= 0; --i) {
    const float t = float(i) / kEndSteps;
    columns.push_back({x1 - end * t, Smooth(t)});
  }
  std::vector<rex::ui::ImmediateVertex> vertices;
  std::vector<uint16_t> indices;
  // One band across the columns, its top and bottom rows at these strengths (0..255 alpha before the taper).
  const auto band = [&](float top, float bottom, float top_alpha, float bottom_alpha) {
    const auto first = uint16_t(vertices.size());
    for (const auto& [px, strength] : columns) {
      vertices.push_back({px, top, 0, 0, kRed | alpha(uint32_t(std::lround(top_alpha * strength))) << 24});
      vertices.push_back({px, bottom, 0, 1, kRed | alpha(uint32_t(std::lround(bottom_alpha * strength))) << 24});
    }
    for (uint16_t c = 0; c + 1 < columns.size(); ++c) {
      const auto a0 = uint16_t(first + c * 2), b0 = uint16_t(a0 + 1), a1 = uint16_t(a0 + 2), b1 = uint16_t(a0 + 3);
      for (const uint16_t v : {a0, a1, b1, a0, b1, b0}) indices.push_back(v);
    }
  };
  // The glow: eased away from the line, above and below it.
  const auto eased = [](float t) { return kGlowAlpha * (1 - t) * (1 - t); };
  const float up = kGlowUp * scale, down = kGlowDown * scale;
  for (int i = kGlowSteps; i > 0; --i) {
    band(line_y - up * i / kGlowSteps, line_y - up * (i - 1) / kGlowSteps, eased(float(i) / kGlowSteps),
         eased(float(i - 1) / kGlowSteps));
  }
  for (int i = 0; i < kGlowSteps; ++i) {
    band(line_y + down * i / kGlowSteps, line_y + down * (i + 1) / kGlowSteps, eased(float(i) / kGlowSteps),
         eased(float(i + 1) / kGlowSteps));
  }
  band(line_y - line / 2, line_y + line / 2, kLineAlpha, kLineAlpha);  // the line
  submit(vertices, indices);
}

}  // namespace fm4_static_recomp::ui
