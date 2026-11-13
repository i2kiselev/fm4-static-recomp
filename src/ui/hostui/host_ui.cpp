// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
#include "ui/hostui/host_ui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <thread>
#include <utility>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/input/state_merge.h>
#include <rex/kernel/xam/module.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/presenter.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

#include "input/controls.h"
#include "input/keycap.h"
#include "ui/hostui/ui_archive.h"
#include "ui/hostui/ui_fonts.h"
#include "ui/hostui/xds_texture.h"

namespace fm4_static_recomp::hostui {
namespace {

using rex::ui::ImmediateTexture;
using rex::ui::ImmediateTextureFilter;
using rex::ui::VirtualKey;

// The title lays its UI out in 1280x720 with a 90 % safe area.
constexpr float kTitleWidth = 1280.0f;
constexpr float kTitleHeight = 720.0f;
constexpr float kSafeLeft = 64.0f;
constexpr float kSafeTop = 36.0f;
constexpr float kSafeRight = kTitleWidth - kSafeLeft;
constexpr float kSafeBottom = kTitleHeight - kSafeTop;

// Layout of FM4's HUD Options screen (300_i_opt_assists_difficulty), in title
// pixels, measured from a capture: a black strip on the left with the screen
// title written upwards, CATEGORY and ADJUSTABLE OPTION columns, a
// description panel on the right and the button help along the bottom.
constexpr float kStripWidth = 105.0f;
constexpr float kStripEdgeWidth = 4.0f;
constexpr float kAccentY = 51.0f;
constexpr float kScreenTitleSize = 64.0f;
constexpr float kScreenTitleTop = 68.0f;
constexpr float kTextSize = 14.0f;
constexpr float kHeaderBaseline = 151.0f;
constexpr float kContentLeft = 118.0f;
constexpr float kLabelRight = 540.0f;
constexpr float kColumnX = 555.0f;
constexpr float kValueLeft = 571.0f;
constexpr float kRowsRight = 980.0f;
constexpr float kRowsTop = 170.0f;
constexpr float kRowPitch = 35.0f;
constexpr float kBarHeight = 33.0f;
constexpr float kRowBaseline = 21.5f;  // below the row's top
constexpr float kArrowBoxWidth = 54.0f;
constexpr float kPanelX = 995.0f;
constexpr float kPanelTextLeft = 1013.0f;
constexpr float kPanelTextRight = 1250.0f;
constexpr float kPanelLinePitch = 21.0f;
constexpr float kPanelFirstBaseline = 199.0f;
constexpr float kPanelAccentHeight = 62.0f;
constexpr float kHelpLineY = 653.0f;
constexpr float kHelpRight = 1210.0f;
constexpr float kHelpBaseline = 677.0f;
constexpr float kHelpSize = 13.0f;
constexpr float kHelpIconSize = 22.0f;
constexpr size_t kVisibleRows = 13;
// The tab titles above the list, between the game's tab icons (Tab_Button_LB / _RB).
constexpr float kTabBaseline = 108.0f;
constexpr float kTabSize = 18.0f;
constexpr float kTabGap = 28.0f;
constexpr float kTabIconWidth = 40.0f;
constexpr float kTabIconHeight = 20.0f;
// Columns of a row of key slots, from the value column's left.
constexpr float kSlotPitch = 200.0f;
// The AUDIO Options screen's slider section (837_i_audio_options, MASTER GAME
// VOLUME), measured from a capture: the slider row sits where the list would
// start, and the list's header, line and rows move down by kSliderSectionHeight.
constexpr float kSliderSectionHeight = 100.0f;
constexpr size_t kSliderSectionRows = 3;
constexpr float kSliderBarLeft = 568.0f;
constexpr float kSliderBarRight = 914.0f;
constexpr float kSliderBarTop = 9.0f;  // below the row's top
constexpr float kSliderBarHeight = 13.0f;
constexpr float kSliderNumberLeft = 927.0f;
// The square beside a value whose change is not applied yet.
constexpr float kUnappliedMark = 6.0f;
// Opening as the game's screens do: the title strip fades in while the rest
// slides in from the right and fades in.
constexpr float kStripFadeMs = 150.0f;
constexpr float kContentFadeMs = 220.0f;
constexpr float kContentSlide = 120.0f;
// Closing: the page goes at once and the dimmed scene fades back in.
constexpr float kCloseFadeMs = 250.0f;

constexpr uint32_t Rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) {
  return r | g << 8 | b << 16 | a << 24;
}
constexpr uint32_t kWhite = Rgba(255, 255, 255);
constexpr uint32_t kLabel = Rgba(240, 240, 240);
constexpr uint32_t kValue = Rgba(173, 173, 175);
constexpr uint32_t kDisabled = Rgba(255, 255, 255, 90);
constexpr uint32_t kHeader = Rgba(149, 148, 118);
constexpr uint32_t kRed = Rgba(212, 46, 25);
constexpr uint32_t kLine = Rgba(119, 119, 111);
constexpr uint32_t kHelpText = Rgba(138, 138, 138);
constexpr uint32_t kScreenTitle = Rgba(45, 45, 45);
constexpr uint32_t kStripEdge = Rgba(111, 111, 111);
constexpr uint32_t kBlack = Rgba(0, 0, 0);
constexpr uint32_t kBackdrop = Rgba(0, 0, 0, 235);
constexpr uint32_t kArrowBoxTint = Rgba(30, 45, 75, 80);
constexpr uint32_t kSliderOutline = Rgba(225, 225, 222);
constexpr uint32_t kSliderFillLow = Rgba(70, 70, 66);
constexpr uint32_t kSliderFillHigh = Rgba(238, 238, 232);

uint32_t Mix(uint32_t from, uint32_t to, float t) {
  uint32_t out = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    const float a = float((from >> shift) & 0xFF);
    const float b = float((to >> shift) & 0xFF);
    out |= uint32_t(std::lround(a + (b - a) * t)) << shift;
  }
  return out;
}

std::unique_ptr<ImmediateTexture> LoadTexture(rex::ui::ImmediateDrawer& drawer,
                                              const UiArchive& archive, const char* name) {
  auto data = archive.Read(name);
  auto image = data ? DecodeXds(*data) : std::nullopt;
  if (!image) {
    REXLOG_WARN("Host UI: could not load texture {}", name);
    return nullptr;
  }
  return drawer.CreateTexture(image->width, image->height, ImmediateTextureFilter::kLinear, false,
                              image->rgba.data());
}

// The tokens of a keyboard driver bind ("Y", "Shift+Y,P"), trimmed.
std::vector<std::string_view> BindTokens(std::string_view bind) {
  std::vector<std::string_view> tokens;
  while (!bind.empty()) {
    const size_t comma = bind.find(',');
    std::string_view token = bind.substr(0, comma);
    bind = comma == std::string_view::npos ? std::string_view() : bind.substr(comma + 1);
    while (token.starts_with(' '))
      token.remove_prefix(1);
    while (token.ends_with(' '))
      token.remove_suffix(1);
    if (!token.empty()) {
      tokens.push_back(token);
    }
  }
  return tokens;
}

}  // namespace

HostUi::HostUi(rex::ReXApp& app, rex::ui::Presenter& presenter, rex::ui::ImmediateDrawer& drawer,
               rex::ui::Window& window, rex::input::InputSystem* input_system,
               std::filesystem::path game_data_root, MenuKeys keys)
    : app_(app),
      presenter_(presenter),
      drawer_(drawer),
      window_(window),
      input_system_(input_system),
      game_data_root_(std::move(game_data_root)),
      keys_(std::move(keys)) {}

HostUi::~HostUi() {
  alive_.reset();
  screen_.reset();
  FinishClose();
}

bool HostUi::LoadAssets() {
  if (assets_loaded_ || assets_failed_) {
    return assets_loaded_;
  }
  const auto started = std::chrono::steady_clock::now();
  const auto ui_root = game_data_root_ / "Media" / "UI";
  auto fonts = UiArchive::Open(ui_root / "Fonts.zip");
  if (!fonts) {
    REXLOG_ERROR("Host UI: cannot open {}", (ui_root / "Fonts.zip").string());
    assets_failed_ = true;
    return false;
  }
  for (size_t face = 0; face < size_t(Face::kCount); ++face) {
    if (auto data = fonts->Read(kUiFontMembers[face])) {
      fonts_[face] = VectorFont::Parse(*data);
    }
    if (!fonts_[face]) {
      REXLOG_ERROR("Host UI: cannot load vector font {}", kUiFontMembers[face]);
      assets_failed_ = true;
      return false;
    }
  }
  const uint8_t white[4] = {255, 255, 255, 255};
  white_ = drawer_.CreateTexture(1, 1, ImmediateTextureFilter::kNearest, false, white);
  LoadKeycaps();
  if (auto textures = UiArchive::Open(ui_root / "Textures.zip")) {
    button_a_ = LoadTexture(drawer_, *textures, "UI4/A_Button_F3.xds");
    button_b_ = LoadTexture(drawer_, *textures, "UI4/B_Button_F3.xds");
    button_x_ = LoadTexture(drawer_, *textures, "UI4/X_Button_F3.xds");
    button_y_ = LoadTexture(drawer_, *textures, "UI4/Y_Button_F3.xds");
    tab_lb_ = LoadTexture(drawer_, *textures, "UI4/Common/Tab_Button_LB.xds");
    tab_rb_ = LoadTexture(drawer_, *textures, "UI4/Common/Tab_Button_RB.xds");
    spinner_ = LoadTexture(drawer_, *textures, "UI4/Common/LOADING_SPINNER_RGB.xds");
    selection_bar_ = LoadTexture(drawer_, *textures, "UI4/Common/SELECTION_BAR_GRADIENT.xds");
    arrow_ = LoadTexture(drawer_, *textures, "UI4/Common/Arrows_Sm_Front.xds");
  } else {
    REXLOG_WARN("Host UI: cannot open Textures.zip; drawing without button icons");
  }
  assets_loaded_ = true;
  REXLOG_INFO("Host UI assets loaded in {} ms: fonts {} ({} glyphs) and {} ({} glyphs)",
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - started)
                  .count(),
              kUiFontMembers[0], fonts_[0]->glyph_count(), kUiFontMembers[1],
              fonts_[1]->glyph_count());
  return true;
}

void HostUi::LoadKeycaps() {
  const auto keycap = [&](std::string_view bind) -> std::unique_ptr<ImmediateTexture> {
    const auto tokens = BindTokens(bind);
    if (tokens.empty()) {
      return nullptr;
    }
    constexpr uint32_t kKeycapSize = 64;
    const std::string labels[] = {input::KeyLabel(tokens.front())};
    const input::Rgba cap =
        input::RenderKeycap(labels, kKeycapSize, *fonts_[size_t(Face::kMedium)]);
    return drawer_.CreateTexture(cap.size, cap.size, ImmediateTextureFilter::kLinear, false,
                                 cap.pixels.data());
  };
  key_accept_ = keycap("Return");
  key_back_ = keycap("Escape");
  key_apply_ = keycap(keys_.apply);
  key_clear_ = keycap(keys_.clear);
  key_previous_tab_ = keycap(keys_.previous_tab);
  key_next_tab_ = keycap(keys_.next_tab);
}

void HostUi::SetMenuKeys(MenuKeys keys) {
  keys_ = std::move(keys);
  if (assets_loaded_) {
    LoadKeycaps();
  }
  RequestPaint();
}

bool HostUi::Open(std::vector<MenuTab> tabs, size_t tab) {
  if (tab >= tabs.size() || !LoadAssets()) {
    return false;
  }
  std::unique_ptr<MenuScreen> screen = tabs[tab].create();
  if (!screen) {
    return false;
  }
  tabs_ = std::move(tabs);
  tab_ = tab;
  screen_ = std::move(screen);
  pointer_first_.reset();
  opened_at_ = std::chrono::steady_clock::now();
  const auto device = input_system_ ? input_system_->ActiveDevice(0) : std::nullopt;
  keyboard_prompts_ = device && device->synthetic;
  REXLOG_INFO("Host UI: opened {}", screen_->title());
  pad_.Reset();
  draining_ = false;
  if (!registered_) {
    registered_ = true;
    SetGuestUiActive(true);
    presenter_.AddUIDrawerFromUIThread(this, kZOrder);
    window_.AddInputListener(this, kZOrder);
  }
  RequestPaint();
  return true;
}

void HostUi::Close() {
  if (applying_) {
    // A row's adjust is running; destroying its screen now would destroy the
    // running function. Apply closes once it returns.
    close_pending_ = true;
    return;
  }
  if (screen_) {
    screen_->Update(true);
  }
  screen_.reset();
  row_rects_.clear();
  pointer_first_.reset();
  tab_rects_.clear();
  if (!registered_) {
    return;
  }
  // Stay registered, drawing nothing, until the input is released.
  draining_ = true;
  drain_started_ = std::chrono::steady_clock::now();
  RequestPaint();
}

void HostUi::FinishClose() {
  draining_ = false;
  held_keys_.clear();
  if (!registered_) {
    return;
  }
  registered_ = false;
  window_.RemoveInputListener(this);
  SetGuestUiActive(false);
  presenter_.RemoveUIDrawerFromUIThread(this);
  REXLOG_INFO("Host UI: closed");
  RequestPaint();
}

bool HostUi::InputReleased() {
  if (!held_keys_.empty()) {
    return false;
  }
  if (!input_system_) {
    return true;
  }
  rex::input::X_INPUT_STATE state = {};
  using rex::X_RESULT;  // X_ERROR_SUCCESS expands to an unqualified cast
  if (input_system_->GetHostPadState(0, &state) != X_ERROR_SUCCESS) {
    return true;
  }
  return !uint16_t(state.gamepad.buttons) && !state.gamepad.left_trigger &&
         !state.gamepad.right_trigger;
}

void HostUi::SetGuestUiActive(bool active) {
  if (guest_ui_active_ == active) {
    return;
  }
  guest_ui_active_ = active;
  if (active) {
    app_.AcquireGuestInputCapture();
    rex::kernel::xam::xeXamSetHostUIActive(true);
  } else {
    app_.ReleaseGuestInputCapture();
    rex::kernel::xam::xeXamSetHostUIActive(false);
  }
}

void HostUi::Leave() { Apply(NavCommand::kClose); }

void HostUi::RequestPaint() { presenter_.RequestUIPaintFromUIThread(); }

// While a drawer is registered the presenter paints on every display refresh
// and on every title frame; waiting here holds the page to the title's rate.
void HostUi::PacePaint() {
  const double hz = std::atof(rex::cvar::GetFlagByName("fh1_guest_vblank_hz").c_str());
  if (hz <= 0.0) {
    return;
  }
  const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(1.0 / hz));
  const auto now = std::chrono::steady_clock::now();
  if (now < next_paint_) {
    std::this_thread::sleep_until(next_paint_);
  }
  next_paint_ = std::max(next_paint_ + period, now);
}

void HostUi::SwitchTab(size_t tab) {
  if (tab == tab_ || tab >= tabs_.size() || screen_->capturing()) {
    return;
  }
  if (!screen_->MayLeave()) {
    RequestPaint();
    return;
  }
  std::unique_ptr<MenuScreen> screen = tabs_[tab].create();
  if (!screen) {
    return;
  }
  screen_->Update(true);
  screen_ = std::move(screen);
  tab_ = tab;
  row_rects_.clear();
  pointer_first_.reset();
  REXLOG_INFO("Host UI: tab {}", screen_->title());
  RequestPaint();
}

void HostUi::Apply(NavCommand command) {
  if (!is_open()) {
    return;
  }
  pointer_first_.reset();
  if (command == NavCommand::kPreviousTab || command == NavCommand::kNextTab) {
    if (tabs_.size() > 1) {
      SwitchTab((tab_ + (command == NavCommand::kNextTab ? 1 : tabs_.size() - 1)) %
                tabs_.size());
    }
    return;
  }
  applying_ = true;
  const MenuScreen::Result result = screen_->Handle(command);
  applying_ = false;
  if (std::exchange(close_pending_, false) || result == MenuScreen::Result::kBack) {
    Close();
    return;
  }
  RequestPaint();
}

void HostUi::PollPad() {
  if (!input_system_) {
    return;
  }
  rex::input::X_INPUT_STATE state = {};
  using rex::X_RESULT;  // X_ERROR_SUCCESS expands to an unqualified cast
  if (input_system_->GetHostPadState(0, &state) != X_ERROR_SUCCESS) {
    return;
  }
  const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - start_time_)
                                .count());
  const bool on_slider = screen_->focus() < screen_->rows().size() &&
                         screen_->rows()[screen_->focus()].slider.has_value();
  if (!rex::input::IsNeutral(state.gamepad)) {
    keyboard_prompts_ = false;
  }
  std::vector<NavCommand> commands =
      pad_.Poll(uint16_t(state.gamepad.buttons), int16_t(state.gamepad.thumb_lx),
                int16_t(state.gamepad.thumb_ly), now, on_slider);
  const int slide_steps = on_slider ? StickSlideSteps(int16_t(state.gamepad.thumb_lx), now) : 0;
  if (!on_slider) {
    slide_owed_ = 0.0f;
  }
  if (commands.empty() && !slide_steps) {
    return;
  }
  // Polling runs inside Draw, where a setting that resizes the window or
  // recreates the swap chain must not apply; run the commands after it.
  window_.app_context().CallInUIThreadDeferred(
      [alive = std::weak_ptr<bool>(alive_), this, commands = std::move(commands), slide_steps] {
        if (alive.expired()) {
          return;
        }
        if (slide_steps && is_open() && screen_->Slide(slide_steps) != MenuScreen::Result::kNone) {
          RequestPaint();
        }
        for (NavCommand command : commands) {
          Apply(command);
        }
      });
}

int HostUi::StickSlideSteps(int16_t thumb_lx, uint64_t now_ms) {
  // XInput's left stick dead zone; past it the speed grows with the square of
  // the deflection, so a light push moves slowly enough to aim.
  constexpr float kDeadZone = 7849.0f;
  constexpr float kStepsPerSecond = 80.0f;
  const float elapsed = std::min(float(now_ms - slide_polled_ms_), 100.0f) / 1000.0f;
  slide_polled_ms_ = now_ms;
  const float deflection =
      std::clamp((std::abs(float(thumb_lx)) - kDeadZone) / (32767.0f - kDeadZone), 0.0f, 1.0f);
  if (deflection <= 0.0f) {
    slide_owed_ = 0.0f;
    return 0;
  }
  slide_owed_ += std::copysign(deflection * deflection * kStepsPerSecond * elapsed,
                               float(thumb_lx));
  const int steps = int(slide_owed_);
  slide_owed_ -= float(steps);
  return steps;
}

HostUi::Canvas HostUi::ComputeCanvas(rex::ui::UIDrawContext& context) const {
  float x = 0.0f, y = 0.0f;
  float width = float(context.render_target_width());
  float height = float(context.render_target_height());
  if (auto rect = presenter_.GetPaintedGuestOutputRectFromUIThread()) {
    x = float(rect->x);
    y = float(rect->y);
    width = float(rect->width);
    height = float(rect->height);
  }
  // Fit the 16:9 title space into the guest image.
  Canvas canvas;
  canvas.scale = std::min(width / kTitleWidth, height / kTitleHeight);
  canvas.x = x + (width - kTitleWidth * canvas.scale) * 0.5f;
  canvas.y = y + (height - kTitleHeight * canvas.scale) * 0.5f;
  return canvas;
}

HostUi::Text& HostUi::TextFor(Face face, float title_pixels) {
  const int pixels = std::max(1, int(std::lround(title_pixels * canvas_.scale)));
  const std::pair<int, int> key(int(face), pixels);
  for (auto& [existing, text] : texts_) {
    if (existing == key) {
      return text;
    }
  }
  atlases_.push_back(std::make_unique<GlyphAtlas>(*fonts_[size_t(face)], float(pixels)));
  Text text;
  text.atlas = atlases_.back().get();
  texts_.emplace_back(key, std::move(text));
  return texts_.back().second;
}

void HostUi::PrepareText(Text& text, std::u32string_view string) {
  text.dirty |= text.atlas->Prepare(string);
  if (text.dirty) {
    // Batches drawn earlier this frame use the old texture; point them at the
    // replacement, which holds a superset of its glyphs.
    auto replacement = drawer_.CreateTexture(text.atlas->width(), text.atlas->height(),
                                             ImmediateTextureFilter::kLinear, false,
                                             text.atlas->rgba().data());
    if (text.texture) {
      for (Batch& batch : batches_) {
        if (batch.texture == text.texture.get()) {
          for (auto& vertex : batch.vertices) {
            vertex.u *= float(text.texture->width) / float(replacement->width);
            vertex.v *= float(text.texture->height) / float(replacement->height);
          }
          batch.texture = replacement.get();
        }
      }
    }
    text.texture = std::move(replacement);
    text.dirty = false;
  }
}

float HostUi::DrawText(Face face, float title_pixels, std::string_view string, float x,
                       float baseline, uint32_t color, float align) {
  Text& text = TextFor(face, title_pixels);
  const std::u32string decoded = DecodeUtf8(string);
  PrepareText(text, decoded);
  const GlyphAtlas& atlas = *text.atlas;
  const float width = atlas.Measure(decoded);
  // Snap the pen to whole pixels; the atlas samples at pixel centres.
  float pen_x = std::round(canvas_.x + x * canvas_.scale - width * align);
  const float pen_y = std::round(canvas_.y + baseline * canvas_.scale);
  const float inverse_width = 1.0f / float(atlas.width());
  const float inverse_height = 1.0f / float(atlas.height());
  for (char32_t code_point : decoded) {
    const GlyphAtlas::Entry* entry = atlas.Find(code_point);
    if (!entry) {
      continue;
    }
    if (entry->width && entry->height) {
      const float x0 = std::round(pen_x) + float(entry->left);
      const float y0 = pen_y + float(entry->top);
      if (batches_.empty() || batches_.back().texture != text.texture.get() ||
          batches_.back().vertices.size() > 60000) {
        batches_.push_back(Batch{.texture = text.texture.get()});
      }
      Batch& batch = batches_.back();
      const auto base = uint16_t(batch.vertices.size());
      const float u0 = float(entry->x) * inverse_width;
      const float v0 = float(entry->y) * inverse_height;
      const float u1 = float(entry->x + entry->width) * inverse_width;
      const float v1 = float(entry->y + entry->height) * inverse_height;
      const float x1 = x0 + float(entry->width);
      const float y1 = y0 + float(entry->height);
      batch.vertices.push_back({x0, y0, u0, v0, Faded(color)});
      batch.vertices.push_back({x1, y0, u1, v0, Faded(color)});
      batch.vertices.push_back({x1, y1, u1, v1, Faded(color)});
      batch.vertices.push_back({x0, y1, u0, v1, Faded(color)});
      for (uint16_t index : {0, 1, 2, 0, 2, 3}) {
        batch.indices.push_back(uint16_t(base + index));
      }
    }
    pen_x += entry->advance;
  }
  return width / canvas_.scale;
}

void HostUi::DrawVerticalText(Face face, float title_pixels, std::string_view string,
                              float baseline_x, float top, uint32_t color) {
  Text& text = TextFor(face, title_pixels);
  const std::u32string decoded = DecodeUtf8(string);
  PrepareText(text, decoded);
  const GlyphAtlas& atlas = *text.atlas;
  const float width = atlas.Measure(decoded);
  // Glyph space (x along the text, y down from the baseline) turned a
  // quarter left: screen x = baseline + y, screen y = start - x.
  const float base_x = std::round(canvas_.x + baseline_x * canvas_.scale);
  const float start_y = std::round(canvas_.y + top * canvas_.scale + width);
  const float inverse_width = 1.0f / float(atlas.width());
  const float inverse_height = 1.0f / float(atlas.height());
  float pen = 0.0f;
  for (char32_t code_point : decoded) {
    const GlyphAtlas::Entry* entry = atlas.Find(code_point);
    if (!entry) {
      continue;
    }
    if (entry->width && entry->height) {
      if (batches_.empty() || batches_.back().texture != text.texture.get() ||
          batches_.back().vertices.size() > 60000) {
        batches_.push_back(Batch{.texture = text.texture.get()});
      }
      Batch& batch = batches_.back();
      const auto base = uint16_t(batch.vertices.size());
      const float gx0 = std::round(pen) + float(entry->left);
      const float gx1 = gx0 + float(entry->width);
      const float gy0 = float(entry->top);
      const float gy1 = gy0 + float(entry->height);
      const float u0 = float(entry->x) * inverse_width;
      const float v0 = float(entry->y) * inverse_height;
      const float u1 = float(entry->x + entry->width) * inverse_width;
      const float v1 = float(entry->y + entry->height) * inverse_height;
      batch.vertices.push_back({base_x + gy0, start_y - gx0, u0, v0, Faded(color)});
      batch.vertices.push_back({base_x + gy0, start_y - gx1, u1, v0, Faded(color)});
      batch.vertices.push_back({base_x + gy1, start_y - gx1, u1, v1, Faded(color)});
      batch.vertices.push_back({base_x + gy1, start_y - gx0, u0, v1, Faded(color)});
      for (uint16_t index : {0, 1, 2, 0, 2, 3}) {
        batch.indices.push_back(uint16_t(base + index));
      }
    }
    pen += entry->advance;
  }
}

std::vector<std::string> HostUi::WrapText(Face face, float title_pixels, std::string_view string,
                                          float width) {
  Text& text = TextFor(face, title_pixels);
  PrepareText(text, DecodeUtf8(string));
  const float limit = width * canvas_.scale;
  std::vector<std::string> lines;
  std::string line;
  size_t at = 0;
  while (at < string.size()) {
    size_t end = string.find(' ', at);
    if (end == std::string_view::npos) {
      end = string.size();
    }
    const std::string_view word = string.substr(at, end - at);
    std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
    if (!line.empty() && text.atlas->Measure(DecodeUtf8(candidate)) > limit) {
      lines.push_back(std::move(line));
      candidate = std::string(word);
    }
    line = std::move(candidate);
    at = end + 1;
  }
  if (!line.empty()) {
    lines.push_back(std::move(line));
  }
  return lines;
}

uint32_t HostUi::Faded(uint32_t color) const {
  const auto alpha = uint32_t(std::lround(float(color >> 24) * fade_));
  return (color & 0x00FFFFFF) | std::min(alpha, UINT32_C(255)) << 24;
}

void HostUi::DrawRect(float x, float y, float width, float height, uint32_t color,
                      ImmediateTexture* texture, float skew) {
  DrawGradient(x, y, width, height, color, color, texture, skew);
}

void HostUi::DrawGradient(float x, float y, float width, float height, uint32_t left_color,
                          uint32_t right_color, ImmediateTexture* texture, float skew,
                          bool mirror) {
  // Solid fills sample a white texel: the drawer's untextured path draws
  // nothing here.
  if (!texture) {
    texture = white_.get();
  }
  if (batches_.empty() || batches_.back().texture != texture ||
      batches_.back().vertices.size() > 60000) {
    batches_.push_back(Batch{.texture = texture});
  }
  Batch& batch = batches_.back();
  const float x0 = canvas_.x + x * canvas_.scale;
  const float y0 = canvas_.y + y * canvas_.scale;
  const float x1 = x0 + width * canvas_.scale;
  const float y1 = y0 + height * canvas_.scale;
  const float shift = skew * canvas_.scale;
  const auto base = uint16_t(batch.vertices.size());
  const float u0 = mirror ? 1.0f : 0.0f;
  const float u1 = 1.0f - u0;
  batch.vertices.push_back({x0 + shift, y0, u0, 0.0f, Faded(left_color)});
  batch.vertices.push_back({x1 + shift, y0, u1, 0.0f, Faded(right_color)});
  batch.vertices.push_back({x1, y1, u1, 1.0f, Faded(right_color)});
  batch.vertices.push_back({x0, y1, u0, 1.0f, Faded(left_color)});
  for (uint16_t index : {0, 1, 2, 0, 2, 3}) {
    batch.indices.push_back(uint16_t(base + index));
  }
}

void HostUi::DrawRotated(float x, float y, float size, float turns, uint32_t color,
                         ImmediateTexture* texture) {
  if (batches_.empty() || batches_.back().texture != texture ||
      batches_.back().vertices.size() > 60000) {
    batches_.push_back(Batch{.texture = texture});
  }
  Batch& batch = batches_.back();
  const float angle = turns * 2.0f * std::numbers::pi_v<float>;
  const float c = std::cos(angle) * size * 0.5f * canvas_.scale;
  const float s = std::sin(angle) * size * 0.5f * canvas_.scale;
  const float cx = canvas_.x + x * canvas_.scale;
  const float cy = canvas_.y + y * canvas_.scale;
  const auto base = uint16_t(batch.vertices.size());
  batch.vertices.push_back({cx - c + s, cy - s - c, 0.0f, 0.0f, Faded(color)});
  batch.vertices.push_back({cx + c + s, cy + s - c, 1.0f, 0.0f, Faded(color)});
  batch.vertices.push_back({cx + c - s, cy + s + c, 1.0f, 1.0f, Faded(color)});
  batch.vertices.push_back({cx - c - s, cy - s + c, 0.0f, 1.0f, Faded(color)});
  for (uint16_t index : {0, 1, 2, 0, 2, 3}) {
    batch.indices.push_back(uint16_t(base + index));
  }
}

void HostUi::RecordLayout(const std::string& title, size_t first_batch, size_t first_vertex) {
  // Once per screen and output size: where the menu drew, against the
  // title's 90 % safe area, so scripted routes can check the layout.
  const std::string key = title + "@" + std::to_string(canvas_.x) + "," +
                          std::to_string(canvas_.y) + "," + std::to_string(canvas_.scale);
  if (key == layout_key_) {
    return;
  }
  layout_key_ = key;
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (size_t b = first_batch; b < batches_.size(); ++b) {
    const auto& vertices = batches_[b].vertices;
    for (size_t v = b == first_batch ? first_vertex : 0; v < vertices.size(); ++v) {
      x0 = std::min(x0, vertices[v].x);
      y0 = std::min(y0, vertices[v].y);
      x1 = std::max(x1, vertices[v].x);
      y1 = std::max(y1, vertices[v].y);
    }
  }
  const float safe_x0 = canvas_.x + kSafeLeft * canvas_.scale;
  const float safe_y0 = canvas_.y + kSafeTop * canvas_.scale;
  const float safe_x1 = canvas_.x + kSafeRight * canvas_.scale;
  const float safe_y1 = canvas_.y + kSafeBottom * canvas_.scale;
  const bool inside = x0 >= safe_x0 && y0 >= safe_y0 && x1 <= safe_x1 && y1 <= safe_y1;
  REXLOG_INFO("Host UI layout: {} scale {:.3f} content {:.0f},{:.0f},{:.0f},{:.0f} safe "
              "{:.0f},{:.0f},{:.0f},{:.0f} inside={}",
              title, canvas_.scale, x0, y0, x1, y1, safe_x0, safe_y0, safe_x1, safe_y1,
              inside ? 1 : 0);
}

void HostUi::Flush() {
  for (const Batch& batch : batches_) {
    if (batch.indices.empty()) {
      continue;
    }
    rex::ui::ImmediateDrawBatch draw_batch;
    draw_batch.vertices = batch.vertices.data();
    draw_batch.vertex_count = int(batch.vertices.size());
    draw_batch.indices = batch.indices.data();
    draw_batch.index_count = int(batch.indices.size());
    drawer_.BeginDrawBatch(draw_batch);
    rex::ui::ImmediateDraw draw;
    draw.count = int(batch.indices.size());
    draw.texture = batch.texture;
    drawer_.Draw(draw);
    drawer_.EndDrawBatch();
  }
  batches_.clear();
}

void HostUi::Draw(rex::ui::UIDrawContext& context) {
  PacePaint();
  if (draining_) {
    // A second is plenty for a press to end; never hold the guest longer.
    constexpr auto kMaximumDrain = std::chrono::seconds(1);
    const auto since_close = std::chrono::steady_clock::now() - drain_started_;
    const float close_ms = std::chrono::duration<float, std::milli>(since_close).count();
    if (close_ms >= kCloseFadeMs && (InputReleased() || since_close > kMaximumDrain)) {
      FinishClose();
      return;
    }
    if (close_ms < kCloseFadeMs && PrepareCanvas(context)) {
      drawer_.Begin(context, float(context.render_target_width()),
                    float(context.render_target_height()));
      fade_ = 1.0f - close_ms / kCloseFadeMs;
      DrawBackdrop();
      fade_ = 1.0f;
      Flush();
      drawer_.End();
    }
    RequestPaint();
    return;
  }
  if (!is_open()) {
    return;
  }
  PollPad();
  // As the pad's commands: a setting the update applies must not apply inside Draw.
  window_.app_context().CallInUIThreadDeferred([alive = std::weak_ptr<bool>(alive_), this] {
    if (!alive.expired() && is_open()) {
      screen_->Update(false);
    }
  });
  DrawMenu(context);
}

bool HostUi::PrepareCanvas(rex::ui::UIDrawContext& context) {
  canvas_ = ComputeCanvas(context);
  if (canvas_.scale <= 0.0f) {
    return false;
  }
  if (canvas_.scale != atlas_scale_) {
    // New output size: rasterize the fonts again at the new pixel sizes.
    batches_.clear();
    texts_.clear();
    atlases_.clear();
    atlas_scale_ = canvas_.scale;
  }
  return true;
}

void HostUi::DrawBackdrop() {
  // Dims the title across the whole guest image.
  if (auto rect = presenter_.GetPaintedGuestOutputRectFromUIThread()) {
    const Canvas saved = canvas_;
    canvas_ = Canvas{float(rect->x), float(rect->y), 1.0f};
    DrawRect(0.0f, 0.0f, float(rect->width), float(rect->height), kBackdrop);
    canvas_ = saved;
  } else {
    DrawRect(0.0f, 0.0f, kTitleWidth, kTitleHeight, kBackdrop);
  }
}

void HostUi::DrawMenu(rex::ui::UIDrawContext& context) {
  if (!PrepareCanvas(context)) {
    return;
  }
  const MenuScreen& screen = *screen_;
  const float open_ms = std::chrono::duration<float, std::milli>(
                            std::chrono::steady_clock::now() - opened_at_)
                            .count();
  const float strip_fade = std::clamp(open_ms / kStripFadeMs, 0.0f, 1.0f);
  const float content_progress = std::clamp(open_ms / kContentFadeMs, 0.0f, 1.0f);
  const float content_ease = 1.0f - (1.0f - content_progress) * (1.0f - content_progress) *
                                        (1.0f - content_progress);
  const float content_x = canvas_.x;

  drawer_.Begin(context, float(context.render_target_width()),
                float(context.render_target_height()));
  fade_ = content_ease;
  DrawBackdrop();
  const size_t content_batch = batches_.size() - 1;
  const size_t content_vertex = batches_.back().vertices.size();

  // The title strip.
  fade_ = strip_fade;
  DrawRect(0.0f, 0.0f, kStripWidth, kTitleHeight, kBlack);
  DrawRect(kStripWidth, 0.0f, kStripEdgeWidth, kTitleHeight, kStripEdge);
  DrawRect(0.0f, kAccentY, kStripWidth, 2.0f, kRed);
  DrawVerticalText(Face::kCondensed, kScreenTitleSize, screen.title(), kStripWidth - 2.0f,
                   kScreenTitleTop, kScreenTitle);

  // Everything else slides in from the right.
  fade_ = content_ease;
  canvas_.x = content_x + kContentSlide * (1.0f - content_ease) * canvas_.scale;

  const auto& rows = screen.rows();
  const bool slider_section = !rows.empty() && rows[0].slider;
  const size_t list_begin = slider_section ? 1 : 0;
  const float list_shift = slider_section ? kSliderSectionHeight : 0.0f;
  const size_t visible_rows = slider_section ? kVisibleRows - kSliderSectionRows : kVisibleRows;

  // Frame lines and column headers.
  DrawRect(kContentLeft, kRowsTop - 1.0f, kTitleWidth - kContentLeft, 1.0f, kLine);
  DrawRect(kColumnX, kRowsTop - 45.0f, 1.0f, 45.0f, kLine);
  if (slider_section) {
    DrawRect(kContentLeft, kRowsTop + list_shift - 1.0f, kRowsRight - kContentLeft, 1.0f, kLine);
    DrawRect(kColumnX, kRowsTop + list_shift - 45.0f, 1.0f, 45.0f, kLine);
  }
  DrawRect(kPanelX, 0.0f, 1.0f, kHelpLineY, kLine);
  DrawRect(kContentLeft, kHelpLineY, kHelpRight - kContentLeft, 1.0f, kLine);
  DrawRect(kHelpRight, kHelpLineY, 1.0f, 33.0f, kLine);
  DrawRect(kHelpRight + 5.0f, kHelpLineY - 1.0f, kTitleWidth - kHelpRight - 5.0f, 2.0f, kRed);
  const std::vector<std::string>& headers = screen.headers();
  DrawText(Face::kMedium, kTextSize, headers.empty() ? "CATEGORY" : headers[0], kLabelRight,
           kHeaderBaseline + list_shift, kHeader, 1.0f);
  if (headers.empty()) {
    DrawText(Face::kMedium, kTextSize, "ADJUSTABLE OPTION", kValueLeft,
             kHeaderBaseline + list_shift, kHeader);
  }
  for (size_t i = 1; i < headers.size(); ++i) {
    DrawText(Face::kMedium, kTextSize, headers[i], kValueLeft + float(i - 1) * kSlotPitch,
             kHeaderBaseline + list_shift, kHeader);
  }
  DrawTabs();

  const size_t first =
      FirstVisibleRow(rows.size(), list_begin, visible_rows, screen.focus(), pointer_first_);
  shown_first_ = first;
  row_rects_.assign(rows.size(), RowRect{0, 0, -1, -1});
  if (slider_section) {
    DrawSliderSection(rows[0], screen.focus() == 0 && rows[0].is_enabled());
    row_rects_[0] = RowRect{canvas_.x + kContentLeft * canvas_.scale,
                            canvas_.y + kRowsTop * canvas_.scale,
                            canvas_.x + kRowsRight * canvas_.scale,
                            canvas_.y + (kRowsTop + kRowPitch) * canvas_.scale};
  }
  for (size_t i = first; i < rows.size() && i < first + visible_rows; ++i) {
    const MenuRow& row = rows[i];
    const bool enabled = row.is_enabled();
    const bool focused = i == screen.focus() && enabled;
    const float top = kRowsTop + list_shift + float(i - first) * kRowPitch;
    const float baseline = top + kRowBaseline;
    // In the gap between the rows; the list's own top line marks the first.
    if (row.divider_above && i > first) {
      DrawRect(kContentLeft, top - 1.5f, kRowsRight - kContentLeft, 1.0f, kLine);
    }
    if (row.heading) {
      DrawText(Face::kCondensed, kTextSize + 3.0f, row.label, kContentLeft + 4.0f, baseline,
               kHeader);
      continue;
    }
    if (focused) {
      DrawRect(kColumnX, top, kRowsRight - kColumnX, kBarHeight, kWhite, selection_bar_.get());
      if (row.adjust) {
        const float box_x = kRowsRight - kArrowBoxWidth;
        DrawRect(box_x, top + 2.0f, kArrowBoxWidth, kBarHeight - 4.0f, kArrowBoxTint);
        if (arrow_) {
          const float arrow_y = top + (kBarHeight - 14.0f) * 0.5f;
          DrawRect(box_x + 14.0f, arrow_y, 8.0f, 14.0f, kWhite, arrow_.get());
          DrawGradient(box_x + 32.0f, arrow_y, 8.0f, 14.0f, kWhite, kWhite, arrow_.get(), 0.0f,
                       true);
        }
      }
      DrawRect(kColumnX, top, kRowsRight - kColumnX, 2.0f, kRed);
      DrawRect(kColumnX, top + kBarHeight - 2.0f, kRowsRight - kColumnX, 2.0f, kRed);
    }
    DrawText(Face::kMedium, kTextSize, row.label, kLabelRight, baseline,
             !enabled ? kDisabled : (focused ? kRed : kLabel), 1.0f);
    if (row.slots) {
      const std::vector<std::string> slots = row.slots();
      const size_t focused_slot = std::min(screen.slot(), slots.empty() ? 0 : slots.size() - 1);
      for (size_t s = 0; s < slots.size(); ++s) {
        const float slot_x = kValueLeft + float(s) * kSlotPitch;
        const bool here = focused && s == focused_slot;
        DrawText(Face::kCondensed, kTextSize + 1.0f, slots[s], slot_x, baseline,
                 !enabled ? kDisabled
                 : here   ? (screen.capturing() ? kRed : kWhite)
                          : kValue);
        if (here) {
          DrawRect(slot_x, top + kBarHeight - 8.0f, kSlotPitch - 30.0f, 2.0f, kRed);
        }
      }
    } else if (row.value) {
      DrawText(Face::kCondensed, kTextSize + 1.0f, row.value(), kValueLeft, baseline,
               !enabled ? kDisabled : (focused ? kWhite : kValue));
    }
    if (row.unapplied && row.unapplied()) {
      DrawRect(kColumnX + 5.0f, top + (kBarHeight - kUnappliedMark) * 0.5f, kUnappliedMark,
               kUnappliedMark, kRed);
    }
    row_rects_[i] = RowRect{canvas_.x + kContentLeft * canvas_.scale,
                            canvas_.y + top * canvas_.scale,
                            canvas_.x + kRowsRight * canvas_.scale,
                            canvas_.y + (top + kRowPitch) * canvas_.scale};
  }

  // The description panel: the focused row's label, description and state.
  if (screen.focus() < rows.size()) {
    const MenuRow& row = rows[screen.focus()];
    DrawText(Face::kMedium, kTextSize, row.label, kPanelTextLeft, kHeaderBaseline, kHeader);
    // A list that scrolls moves the accent down its height with the scroll.
    float accent_top = kRowsTop;
    if (const size_t list_rows = rows.size() - list_begin; list_rows > visible_rows) {
      const float list_bottom = kRowsTop + list_shift + float(visible_rows) * kRowPitch;
      accent_top += (list_bottom - kPanelAccentHeight - kRowsTop) * float(first - list_begin) /
                    float(list_rows - visible_rows);
    }
    DrawRect(kPanelX - 5.0f, accent_top, 4.0f, kPanelAccentHeight, kRed);
    float baseline = kPanelFirstBaseline;
    const auto paragraph = [&](const std::string& text, uint32_t color) {
      if (text.empty()) {
        return;
      }
      for (const std::string& line :
           WrapText(Face::kMedium, kTextSize, text, kPanelTextRight - kPanelTextLeft)) {
        DrawText(Face::kMedium, kTextSize, line, kPanelTextLeft, baseline, color);
        baseline += kPanelLinePitch;
      }
      baseline += kPanelLinePitch * 0.5f;
    };
    paragraph(row.description, kLabel);
    paragraph(row.note ? row.note() : std::string(), kLabel);
    paragraph(row.unapplied && row.unapplied() ? std::string("Not applied yet.") : std::string(),
              kRed);
    paragraph(row.badge ? row.badge() : std::string(), kHeader);
  }

  // Button help, right-aligned against the end of its line.
  struct Help {
    ImmediateTexture* icon;
    const char* label;
  };
  const MenuStatus status = screen.status();
  std::vector<Help> help;
  // Escape is a key like any other while a key is awaited.
  std::string clear_label;
  if (screen.capturing()) {
    help.push_back(keyboard_prompts_ ? Help{nullptr, "CLICK TO CANCEL"}
                                     : Help{button_b_.get(), "CANCEL"});
  } else {
    help.push_back({keyboard_prompts_ ? key_accept_.get() : button_a_.get(), "CHANGE"});
    if (screen.focus() < rows.size()) {
      const MenuRow& row = rows[screen.focus()];
      if (row.slots && row.clear_label) {
        clear_label =
            row.clear_label(std::min(screen.slot(), std::max<size_t>(row.slots().size(), 1) - 1));
      }
    }
    if (!clear_label.empty()) {
      help.push_back({keyboard_prompts_ ? key_clear_.get() : button_x_.get(), clear_label.c_str()});
    }
    if (screen.has_apply()) {
      help.push_back({keyboard_prompts_ ? key_apply_.get() : button_y_.get(), "SAVE & APPLY"});
    }
    help.push_back({keyboard_prompts_ ? key_back_.get() : button_b_.get(),
                    status.warning ? "DISCARD" : "BACK"});
  }
  float help_x = kHelpRight - 16.0f;
  for (size_t i = help.size(); i-- > 0;) {
    help_x -= DrawText(Face::kMedium, kHelpSize, help[i].label, help_x, kHelpBaseline, kHelpText,
                       1.0f);
    if (help[i].icon) {
      help_x -= 10.0f + kHelpIconSize;
      DrawRect(help_x, kHelpBaseline - 5.0f - kHelpIconSize * 0.5f, kHelpIconSize, kHelpIconSize,
               kWhite, help[i].icon);
    }
    help_x -= 26.0f;
  }

  // The screen's state; while busy, the game's spinner turns a segment at a
  // time as the game's does.
  if (status.busy && spinner_) {
    const auto step = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - start_time_)
                          .count() /
                      80;
    DrawRotated(kContentLeft + 22.0f, kHelpBaseline - 5.0f, 26.0f, float(step % 10) / 10.0f,
                kWhite, spinner_.get());
  }
  if (!status.text.empty()) {
    DrawText(Face::kMedium, kHelpSize, status.text, kContentLeft + 44.0f, kHelpBaseline,
             status.warning ? kRed : kLabel);
  }

  canvas_.x = content_x;
  fade_ = 1.0f;
  if (content_progress >= 1.0f) {
    RecordLayout(screen.title(), content_batch, content_vertex);
  }
  Flush();
  drawer_.End();
  // Keep painting while open so the pad is polled even when the title
  // presents nothing new.
  RequestPaint();
}

void HostUi::DrawTabs() {
  tab_rects_.assign(tabs_.size(), RowRect{0, 0, -1, -1});
  if (tabs_.size() < 2) {
    return;
  }
  ImmediateTexture* const previous = keyboard_prompts_ ? key_previous_tab_.get() : tab_lb_.get();
  ImmediateTexture* const next = keyboard_prompts_ ? key_next_tab_.get() : tab_rb_.get();
  const float icon_width = keyboard_prompts_ ? kHelpIconSize : kTabIconWidth;
  const float icon_height = keyboard_prompts_ ? kHelpIconSize : kTabIconHeight;
  const float icon_top = kTabBaseline - 6.0f - icon_height * 0.5f;
  float x = kContentLeft;
  if (previous) {
    DrawRect(x, icon_top, icon_width, icon_height, kWhite, previous);
  }
  x += icon_width + 16.0f;
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const bool shown = i == tab_;
    const float width = DrawText(Face::kCondensed, kTabSize, tabs_[i].title, x, kTabBaseline,
                                 shown ? kWhite : kValue);
    if (shown) {
      DrawRect(x, kTabBaseline + 6.0f, width, 2.0f, kRed);
    }
    tab_rects_[i] = RowRect{canvas_.x + (x - 6.0f) * canvas_.scale,
                            canvas_.y + (kTabBaseline - 22.0f) * canvas_.scale,
                            canvas_.x + (x + width + 6.0f) * canvas_.scale,
                            canvas_.y + (kTabBaseline + 10.0f) * canvas_.scale};
    x += width + kTabGap;
  }
  if (next) {
    DrawRect(x - kTabGap + 16.0f, icon_top, icon_width, icon_height, kWhite, next);
  }
}

void HostUi::DrawSliderSection(const MenuRow& row, bool focused) {
  const MenuSlider& slider = *row.slider;
  const auto [low, high] = slider.range();
  const int value = slider.get();
  // The header names the screen, as AUDIO's does, over the range's ends.
  DrawText(Face::kMedium, kTextSize, screen_->title(), kLabelRight, kHeaderBaseline, kHeader,
           1.0f);
  DrawText(Face::kMedium, kTextSize, std::to_string(low), kValueLeft, kHeaderBaseline, kHeader);
  DrawText(Face::kMedium, kTextSize, std::to_string(high), kSliderBarRight, kHeaderBaseline,
           kHeader, 1.0f);

  const float top = kRowsTop;
  const bool enabled = row.is_enabled();
  if (focused) {
    DrawRect(kColumnX, top, kSliderNumberLeft - kColumnX, kBarHeight, kWhite,
             selection_bar_.get());
    DrawRect(kColumnX, top, kRowsRight - kColumnX, 2.0f, kRed);
    DrawRect(kColumnX, top + kBarHeight - 2.0f, kRowsRight - kColumnX, 2.0f, kRed);
  }
  DrawRect(kSliderNumberLeft, top + 2.0f, kRowsRight - kSliderNumberLeft, kBarHeight - 4.0f,
           kBlack);
  DrawText(Face::kMedium, kTextSize, row.label, kLabelRight, top + kRowBaseline,
           !enabled ? kDisabled : (focused ? kRed : kLabel), 1.0f);

  const float bar_top = top + kSliderBarTop;
  const float bar_width = kSliderBarRight - kSliderBarLeft;
  DrawRect(kSliderBarLeft, bar_top, bar_width, kSliderBarHeight, kSliderOutline);
  DrawRect(kSliderBarLeft + 1.0f, bar_top + 1.0f, bar_width - 2.0f, kSliderBarHeight - 2.0f,
           kBlack);
  const float fraction =
      high > low ? std::clamp(float(value - low) / float(high - low), 0.0f, 1.0f) : 1.0f;
  const float fill_width = (bar_width - 4.0f) * fraction;
  if (fill_width > 0.0f) {
    DrawGradient(kSliderBarLeft + 2.0f, bar_top + 2.0f, fill_width, kSliderBarHeight - 4.0f,
                 kSliderFillLow, Mix(kSliderFillLow, kSliderFillHigh, fraction));
  }
  DrawText(Face::kCondensed, kTextSize + 3.0f, std::to_string(value), kRowsRight - 2.0f,
           top + kRowBaseline + 1.0f, !enabled ? kDisabled : kWhite, 1.0f);
  if (row.unapplied && row.unapplied()) {
    DrawRect(kSliderNumberLeft + 5.0f, top + (kBarHeight - kUnappliedMark) * 0.5f, kUnappliedMark,
             kUnappliedMark, kRed);
  }
}

void HostUi::OnKeyDown(rex::ui::KeyEvent& e) {
  const VirtualKey key = e.virtual_key();
  if (key >= VirtualKey::kF1 && key <= VirtualKey::kF24) {
    return;  // keybinds stay live
  }
  if (std::find(held_keys_.begin(), held_keys_.end(), int(key)) == held_keys_.end()) {
    held_keys_.push_back(int(key));
  }
  if (!is_open()) {
    // The repeats of a held closing key, latched by the driver, would press
    // its button in the game once the page lets go.
    if (draining_) {
      e.set_handled(true);
    }
    return;
  }
  e.set_handled(true);
  keyboard_prompts_ = true;
  if (screen_->capturing()) {
    if (const std::string name = input::BindKeyName(key); !e.prev_state() && !name.empty()) {
      captured_key_ = int(key);
      screen_->CaptureKey(name);
      RequestPaint();
    }
    return;
  }
  // The repeats of a key just captured (an arrow key) must not move the menu.
  if (int(key) == captured_key_) {
    return;
  }
  if (const auto command = KeyCommand(keys_, key, e.prev_state())) {
    Apply(*command);
  }
}

void HostUi::OnKeyUp(rex::ui::KeyEvent& e) {
  held_keys_.erase(std::remove(held_keys_.begin(), held_keys_.end(), int(e.virtual_key())),
                   held_keys_.end());
  if (int(e.virtual_key()) == captured_key_) {
    captured_key_ = -1;
  }
  // Releases go on to the keyboard driver: the key that opened the page would
  // otherwise stay held there and press its button when the page closes.
}

void HostUi::OnKeyChar(rex::ui::KeyEvent& e) {
  if (is_open()) {
    e.set_handled(true);
  }
}

void HostUi::OnMouseMove(rex::ui::MouseEvent& e) {
  if (!is_open()) {
    return;
  }
  e.set_handled(true);
  if (screen_->capturing()) {
    return;
  }
  for (size_t i = 0; i < row_rects_.size(); ++i) {
    const RowRect& rect = row_rects_[i];
    if (float(e.x()) >= rect.x0 && float(e.x()) < rect.x1 && float(e.y()) >= rect.y0 &&
        float(e.y()) < rect.y1) {
      if (screen_->focus() != i && screen_->SetFocus(i)) {
        pointer_first_ = shown_first_;
        RequestPaint();
      }
      return;
    }
  }
}

void HostUi::OnMouseDown(rex::ui::MouseEvent& e) {
  if (!is_open()) {
    return;
  }
  e.set_handled(true);
  keyboard_prompts_ = true;
  // Any click ends a key capture, as back on the pad does.
  if (e.button() == rex::ui::MouseEvent::Button::kRight || screen_->capturing()) {
    Apply(NavCommand::kBack);
    return;
  }
  if (e.button() != rex::ui::MouseEvent::Button::kLeft) {
    return;
  }
  const auto inside = [&](const RowRect& rect) {
    return float(e.x()) >= rect.x0 && float(e.x()) < rect.x1 && float(e.y()) >= rect.y0 &&
           float(e.y()) < rect.y1;
  };
  for (size_t i = 0; i < tab_rects_.size(); ++i) {
    if (inside(tab_rects_[i])) {
      SwitchTab(i);
      return;
    }
  }
  const size_t focus = screen_->focus();
  if (focus >= row_rects_.size() || !inside(row_rects_[focus])) {
    return;
  }
  // Clicking a key slot picks it; clicking the label picks the focused one.
  if (const MenuRow& row = screen_->rows()[focus]; row.slots) {
    const float column = (float(e.x()) - canvas_.x) / canvas_.scale - kValueLeft;
    if (column >= 0.0f) {
      screen_->SetSlot(std::min(size_t(column / kSlotPitch),
                                std::max<size_t>(row.slots().size(), 1) - 1));
    }
    Apply(NavCommand::kAccept);
    return;
  }
  // Clicking a slider's bar sets the value under the pointer.
  if (screen_->rows()[focus].slider) {
    const float bar_x0 = canvas_.x + (kSliderBarLeft + 2.0f) * canvas_.scale;
    const float bar_x1 = canvas_.x + (kSliderBarRight - 2.0f) * canvas_.scale;
    const float slack = 6.0f * canvas_.scale;
    if (float(e.x()) >= bar_x0 - slack && float(e.x()) < bar_x1 + slack &&
        screen_->SlideTo((float(e.x()) - bar_x0) / (bar_x1 - bar_x0)) !=
            MenuScreen::Result::kNone) {
      RequestPaint();
    }
    return;
  }
  // Clicking a row steps it: on the left arrow backwards, anywhere else
  // forwards.
  const float arrow_x0 = canvas_.x + (kRowsRight - kArrowBoxWidth) * canvas_.scale;
  const float arrow_split = canvas_.x + (kRowsRight - kArrowBoxWidth * 0.5f) * canvas_.scale;
  const bool back = float(e.x()) >= arrow_x0 && float(e.x()) < arrow_split;
  Apply(back ? NavCommand::kLeft : NavCommand::kRight);
}

void HostUi::OnMouseWheel(rex::ui::MouseEvent& e) {
  if (!is_open()) {
    return;
  }
  e.set_handled(true);
  keyboard_prompts_ = true;
  if (e.scroll_y() > 0) {
    Apply(NavCommand::kUp);
  } else if (e.scroll_y() < 0) {
    Apply(NavCommand::kDown);
  }
}

}  // namespace fm4_static_recomp::hostui
