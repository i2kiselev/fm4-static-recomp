// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <rex/ui/immediate_drawer.h>
#include <rex/ui/ui_drawer.h>
#include <rex/ui/window_listener.h>

#include "ui/hostui/glyph_atlas.h"
#include "ui/hostui/menu.h"
#include "ui/hostui/vector_font.h"

namespace rex {
class ReXApp;
namespace input {
class InputSystem;
}
namespace ui {
class Presenter;
class Window;
}  // namespace ui
}  // namespace rex

namespace fm4_static_recomp::hostui {

// One tab of the host menu: its screen is made each time the tab is shown.
struct MenuTab {
  std::string title;
  std::function<std::unique_ptr<MenuScreen>()> create;
};

// A host-drawn menu screen over the title, after FM4's HUD Options screen:
// drawn with the title's own vector fonts and UI textures, rasterized at load
// from the game data, and laid out in the title's 1280x720 space inside the
// painted guest output so it scales with the window.
//
// While open the title sees system UI (XN_SYS_UI), the guest reads untouched
// pads and no mouse-and-keyboard input, and the menu takes pad, keyboard and
// mouse input itself. It is a presenter drawer only while open or closing:
// any registered drawer moves presentation to the UI thread. Everything runs
// on the UI thread; drawing never runs guest code.
class HostUi final : public rex::ui::UIDrawer, public rex::ui::WindowInputListener {
 public:
  // Above the guest's input listeners.
  static constexpr size_t kZOrder = 32;

  HostUi(rex::ReXApp& app, rex::ui::Presenter& presenter, rex::ui::ImmediateDrawer& drawer,
         rex::ui::Window& window, rex::input::InputSystem* input_system,
         std::filesystem::path game_data_root, MenuKeys keys);
  ~HostUi() override;

  bool is_open() const { return bool(screen_); }
  // Closed, but still holding the input until the closing press is released.
  bool is_closing() const { return draining_; }
  // Opens on `tab`; LB and RB (or a click on a title) switch tabs, leaving the shown one as
  // back would. Fails (and logs) when the title's UI assets cannot be loaded.
  bool Open(std::vector<MenuTab> tabs, size_t tab);
  void SetMenuKeys(MenuKeys keys);
  // The guest gets its input back only once no pad button or key is held,
  // so the press that closed the menu does not also reach the title.
  void Close();
  // Leaves as Start does; the screen may stay open (unapplied changes).
  void Leave();

  void Draw(rex::ui::UIDrawContext& context) override;

  void OnKeyDown(rex::ui::KeyEvent& e) override;
  void OnKeyUp(rex::ui::KeyEvent& e) override;
  void OnKeyChar(rex::ui::KeyEvent& e) override;
  void OnMouseDown(rex::ui::MouseEvent& e) override;
  void OnMouseMove(rex::ui::MouseEvent& e) override;
  void OnMouseWheel(rex::ui::MouseEvent& e) override;

 private:
  // Swiss 721 Bold Condensed and Swiss 721 Medium, as the game maps them.
  enum class Face { kCondensed, kMedium, kCount };

  struct Canvas {
    float x = 0.0f;  // render-target pixels of the 1280x720 origin
    float y = 0.0f;
    float scale = 1.0f;  // render-target pixels per title pixel
  };

  struct Batch {
    std::vector<rex::ui::ImmediateVertex> vertices;
    std::vector<uint16_t> indices;
    rex::ui::ImmediateTexture* texture = nullptr;
  };

  struct Text {
    GlyphAtlas* atlas = nullptr;
    std::unique_ptr<rex::ui::ImmediateTexture> texture;
    bool dirty = true;
  };

  bool LoadAssets();
  void LoadKeycaps();
  void SwitchTab(size_t tab);
  void DrawMenu(rex::ui::UIDrawContext& context);
  void DrawTabs();
  // The game's tuning slider section above the list, for a slider first row.
  void DrawSliderSection(const MenuRow& row, bool focused);
  void DrawBackdrop();
  bool PrepareCanvas(rex::ui::UIDrawContext& context);
  void Apply(NavCommand command);
  void PollPad();
  // Whole steps for the focused slider from the stick's deflection since the
  // last poll.
  int StickSlideSteps(int16_t thumb_lx, uint64_t now_ms);
  void RequestPaint();
  void PacePaint();
  void SetGuestUiActive(bool active);
  void FinishClose();
  bool InputReleased();

  Canvas ComputeCanvas(rex::ui::UIDrawContext& context) const;
  Text& TextFor(Face face, float title_pixels);
  void PrepareText(Text& text, std::u32string_view string);
  float DrawText(Face face, float title_pixels, std::string_view string, float x, float baseline,
                 uint32_t color, float align = 0.0f);
  // Reads upwards, ending at `top`, with the glyph tops to the left of the
  // baseline at `baseline_x`, as the game's screen titles.
  void DrawVerticalText(Face face, float title_pixels, std::string_view string, float baseline_x,
                        float top, uint32_t color);
  // Splits at spaces into lines no wider than `width` title pixels.
  std::vector<std::string> WrapText(Face face, float title_pixels, std::string_view string,
                                    float width);
  // `color` with its alpha scaled by the current fade.
  uint32_t Faded(uint32_t color) const;
  void DrawRect(float x, float y, float width, float height, uint32_t color,
                rex::ui::ImmediateTexture* texture = nullptr, float skew = 0.0f);
  void DrawGradient(float x, float y, float width, float height, uint32_t left_color,
                    uint32_t right_color, rex::ui::ImmediateTexture* texture = nullptr,
                    float skew = 0.0f, bool mirror = false);
  // A square `size` wide centred on (x, y), turned by `turns` of a circle.
  void DrawRotated(float x, float y, float size, float turns, uint32_t color,
                   rex::ui::ImmediateTexture* texture);
  void Flush();
  void RecordLayout(const std::string& title, size_t first_batch, size_t first_vertex);

  rex::ReXApp& app_;
  rex::ui::Presenter& presenter_;
  rex::ui::ImmediateDrawer& drawer_;
  rex::ui::Window& window_;
  rex::input::InputSystem* input_system_;
  std::filesystem::path game_data_root_;
  MenuKeys keys_;

  std::vector<MenuTab> tabs_;
  size_t tab_ = 0;
  std::unique_ptr<MenuScreen> screen_;
  PadNavigator pad_;
  // Slider steps owed to the stick's deflection, and when it was last polled.
  float slide_owed_ = 0.0f;
  uint64_t slide_polled_ms_ = 0;
  // Whether the drawer, the input listener and the guest input capture are
  // registered: from Open until the input is released after Close.
  bool registered_ = false;
  // Closed, waiting for held buttons and keys to be released.
  bool draining_ = false;
  std::chrono::steady_clock::time_point drain_started_;
  std::vector<int> held_keys_;
  // The key a capture took, until released.
  int captured_key_ = -1;
  std::chrono::steady_clock::time_point next_paint_;
  // Expires with this object, for deferred work queued from Draw.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  bool applying_ = false;
  bool close_pending_ = false;
  bool guest_ui_active_ = false;
  // The last device used: keyboard or mouse, or a pad.
  bool keyboard_prompts_ = false;
  std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();

  // Assets, loaded on first open.
  bool assets_loaded_ = false;
  bool assets_failed_ = false;
  std::optional<VectorFont> fonts_[size_t(Face::kCount)];
  std::unique_ptr<rex::ui::ImmediateTexture> white_;
  std::unique_ptr<rex::ui::ImmediateTexture> button_a_;
  std::unique_ptr<rex::ui::ImmediateTexture> button_b_;
  std::unique_ptr<rex::ui::ImmediateTexture> button_x_;
  std::unique_ptr<rex::ui::ImmediateTexture> button_y_;
  std::unique_ptr<rex::ui::ImmediateTexture> tab_lb_;
  std::unique_ptr<rex::ui::ImmediateTexture> tab_rb_;
  // Keycaps of the menu's own keys, for the help while the keyboard leads.
  std::unique_ptr<rex::ui::ImmediateTexture> key_accept_;
  std::unique_ptr<rex::ui::ImmediateTexture> key_back_;
  std::unique_ptr<rex::ui::ImmediateTexture> key_apply_;
  std::unique_ptr<rex::ui::ImmediateTexture> key_clear_;
  std::unique_ptr<rex::ui::ImmediateTexture> key_previous_tab_;
  std::unique_ptr<rex::ui::ImmediateTexture> key_next_tab_;
  std::unique_ptr<rex::ui::ImmediateTexture> spinner_;
  std::unique_ptr<rex::ui::ImmediateTexture> selection_bar_;
  std::unique_ptr<rex::ui::ImmediateTexture> arrow_;
  std::chrono::steady_clock::time_point opened_at_{};
  // Alpha scale of what is drawn, for the opening fade.
  float fade_ = 1.0f;

  // Per-draw state.
  Canvas canvas_;
  float atlas_scale_ = 0.0f;
  std::vector<std::unique_ptr<GlyphAtlas>> atlases_;
  std::vector<std::pair<std::pair<int, int>, Text>> texts_;  // (face, pixels) -> text
  std::vector<Batch> batches_;
  // Row hit rectangles in window pixels from the latest draw, for the mouse.
  struct RowRect {
    float x0, y0, x1, y1;
  };
  std::vector<RowRect> row_rects_;
  std::vector<RowRect> tab_rects_;
  // The list's first row in the latest draw, and the one the pointer keeps while it moves the
  // focus; keys, the wheel and a new screen let the list follow the focus again.
  size_t shown_first_ = 0;
  std::optional<size_t> pointer_first_;
  // Screen and output size of the last recorded layout event.
  std::string layout_key_;
};

}  // namespace fm4_static_recomp::hostui
