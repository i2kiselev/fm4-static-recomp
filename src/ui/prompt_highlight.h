// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include <rex/ui/immediate_drawer.h>
#include <rex/ui/presenter.h>
#include <rex/ui/ui_drawer.h>

#include "input/menu_hit.h"

namespace fm4_static_recomp::ui {

// The mouse's highlight on the prompt under the pointer (#76): the game never lights its prompts (a pad cannot
// point at them), so the game's red line, fading at its ends, with a faint glow above it, marks the one a click would press.
// A carousel's tile is lit whole instead, a soft white veil like the selected tile's face.
// Registered with the presenter only while there is one to draw (a drawer makes it paint every refresh).
// UI thread only.
class PromptHighlight final : public rex::ui::UIDrawer {
 public:
  static constexpr size_t kZOrder = 1;  // under the PC OPTIONS pages

  PromptHighlight(rex::ui::Presenter& presenter, rex::ui::ImmediateDrawer& drawer);
  ~PromptHighlight();

  // The prompt's box in the game's UI space (1280 x 720, centred, y up); nullopt hides it.
  void Show(std::optional<input::UiRect> box);
  void Draw(rex::ui::UIDrawContext& context) override;

 private:
  rex::ui::Presenter& presenter_;
  rex::ui::ImmediateDrawer& drawer_;
  std::unique_ptr<rex::ui::ImmediateTexture> white_;
  std::optional<input::UiRect> box_;
  bool registered_ = false;
  std::chrono::steady_clock::time_point shown_at_{};
};

}  // namespace fm4_static_recomp::ui
