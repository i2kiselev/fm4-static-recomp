#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/kernel/xboxkrnl/io.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/xmemory.h>
#include <rex/ui/flags.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/overlay/achievement_notification.h>
#include <rex/ui/vulkan/provider.h>
#include <rex/ui/vulkan/reflex.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

#include "built_in_defaults.h"
#include "config/host_config.h"
#include "disc2_install.h"
#include "file_open_log.h"
#include "game_hooks.h"
#include "game_language.h"
#include "input/controls.h"
#include "input/input_context.h"
#include "input/keyboard_ramp.h"
#include "input/prompt_strings.h"
#include "input/prompt_swap.h"
#include "scripted_input.h"
#include "shader_cache/cache_writer.h"
#include "shader_cache/disc_shaders.h"
#include "tool_mode.h"
#include "ui/graphics_page.h"
#include "ui/hostui/host_ui.h"
#include "ui/hostui/ui_fonts.h"

REXCVAR_DECLARE(std::string, fm4_config_root);
REXCVAR_DECLARE(std::string, fm4_write_graphics_schema);
REXCVAR_DECLARE(std::string, fm4_dump_disc_shaders);
REXCVAR_DECLARE(std::string, fm4_build_shader_cache);
REXCVAR_DECLARE(std::string, fm4_shader_list_dir);
REXCVAR_DECLARE(std::string, fm4_export_ui_fonts);

class Fm4App final : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& context) {
    return std::unique_ptr<Fm4App>(new Fm4App(context, "fm4_static_recomp", PPCImageConfig));
  }

 protected:
  void OnConfigurePaths(rex::PathConfig& paths) override {
    const std::string configured = REXCVAR_GET(fm4_config_root);
    const std::filesystem::path config_root =
        configured.empty() ? paths.user_data_root.parent_path() / "config"
                           : std::filesystem::path(configured);
    graphics_config_ =
        std::make_unique<fm4_static_recomp::config::HostConfig>(config_root / "graphics.toml");
    controls_config_ =
        std::make_unique<fm4_static_recomp::config::HostConfig>(config_root / "controls.toml");
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    const fm4_static_recomp::ToolMode mode = fm4_static_recomp::CurrentToolMode();
    if (mode == fm4_static_recomp::ToolMode::kInstall ||
        mode == fm4_static_recomp::ToolMode::kShaderCache ||
        mode == fm4_static_recomp::ToolMode::kUiFonts) {
      config.gpu_plugin.clear();
      config.audio_factory = nullptr;
      config.tool_mode = true;
      return;
    }
    config.gpu_plugin = "fh1";
    fm4_static_recomp::ConfigureScriptedInput(config);
    fm4_static_recomp::ApplyBuiltInDefaults(fm4_static_recomp::DefaultStage::kBeforeGpuPlugin);
    // The GPU settings are registered by the plugin, so it is loaded here
    // rather than by the app, and the saved values reach them before the GPU
    // is set up.
    config.graphics =
        rex::system::LoadGpuPlugin(config.gpu_plugin, rex::cvar::GetFlagByName("gpu_backend"));
    if (mode == fm4_static_recomp::ToolMode::kSchema) {
      return;
    }
    fm4_static_recomp::ApplyBuiltInDefaults(fm4_static_recomp::DefaultStage::kAfterGpuPlugin);
    if (graphics_config_->Load()) {
      fm4_static_recomp::ui::ApplySavedGraphicsSettings(*graphics_config_);
    } else {
      REXLOG_ERROR("Graphics settings: cannot read {}", graphics_config_->path().string());
    }
    fm4_static_recomp::InstallPixelShaderSwitches();
    rex::kernel::xboxkrnl::SetGuestFileOpenObserver([](std::string_view guest_path) {
      fm4_static_recomp::NoteGuestFileOpen(guest_path);
      fm4_static_recomp::LogFileOpen(guest_path);
    });
    if (controls_config_->Load()) {
      controls_ = fm4_static_recomp::input::LoadControls(*controls_config_);
    } else {
      REXLOG_ERROR("Controls: cannot read {}", controls_config_->path().string());
    }
  }

  bool WantsPresentation() const override {
    return fm4_static_recomp::CurrentToolMode() == fm4_static_recomp::ToolMode::kNone;
  }

  bool ConstructRuntime(const rex::PathConfig& paths) override {
    if (fm4_static_recomp::CurrentToolMode() == fm4_static_recomp::ToolMode::kSchema) {
      const std::filesystem::path path = std::string(REXCVAR_GET(fm4_write_graphics_schema));
      if (fm4_static_recomp::ui::WriteGraphicsSchema(path)) {
        REXLOG_INFO("Graphics schema written to {}", path.string());
      } else {
        REXLOG_ERROR("Graphics schema: cannot write {}", path.string());
      }
      return true;
    }
    if (fm4_static_recomp::CurrentToolMode() == fm4_static_recomp::ToolMode::kUiFonts) {
      const std::filesystem::path out_dir = std::string(REXCVAR_GET(fm4_export_ui_fonts));
      std::string error;
      if (fm4_static_recomp::hostui::ExportUiFonts(
              paths.game_data_root / "Media" / "UI" / "Fonts.zip", out_dir, &error)) {
        REXLOG_INFO("UI fonts: exported {} to {}",
                    std::size(fm4_static_recomp::hostui::kUiFontMembers), out_dir.string());
      } else {
        REXLOG_ERROR("UI fonts: {}", error);
      }
      return true;
    }
    if (fm4_static_recomp::CurrentToolMode() == fm4_static_recomp::ToolMode::kNone &&
        !fm4_static_recomp::ApplyGameLanguage()) {
      return false;
    }
    if (!rex::ReXApp::ConstructRuntime(paths)) {
      return false;
    }
    if (fm4_static_recomp::CurrentToolMode() == fm4_static_recomp::ToolMode::kShaderCache) {
      return RunShaderCacheTool();
    }
    return true;
  }

  void LaunchModule() override {
    if (fm4_static_recomp::CurrentToolMode() == fm4_static_recomp::ToolMode::kNone) {
      rex::ReXApp::LaunchModule();
      return;
    }
    app_context().RequestDeferredQuit();
  }

  void OnPostSetup() override {
    fm4_static_recomp::InstallContentPackages(*runtime());
    if (fm4_static_recomp::CurrentToolMode() != fm4_static_recomp::ToolMode::kNone) {
      return;
    }
    if (rex::cvar::GetFlagByName("gpu_backend") == "vulkan" && runtime()->graphics_system()) {
      const auto* const provider = static_cast<const rex::ui::vulkan::VulkanProvider*>(
          runtime()->graphics_system()->provider());
      fm4_static_recomp::ApplyGpuVendorDefaults(provider->vulkan_device()->properties().vendorID);
    }
    if (rex::ui::Window* const window = this->window()) {
      window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kAutoHidden);
      fm4_static_recomp::ui::LimitFrameRateToDisplay(
          fm4_static_recomp::ui::DisplayRefreshHz(window->GetNativeWindowHandle()));
    }
    fm4_static_recomp::StartCaptureWatcher([this]() -> rex::ui::Presenter* {
      return runtime() && runtime()->graphics_system() ? runtime()->graphics_system()->presenter()
                                                       : nullptr;
    });
    fm4_static_recomp::SetGraphicsPageOpener([this] {
      if (rex::ui::Window* const window = this->window()) {
        window->app_context().CallInUIThreadDeferred([this] { OpenGraphicsPage(); });
      }
    });
    InstallKeyboard();
    // The SDK's pad_chord_game_menu runs the bind of this name.
    rex::ui::RegisterBind("bind_game_menu", "F6", "Toggle the GRAPHICS page", [this] {
      // A START pressed before BACK has already closed the page by itself.
      if (host_ui_ && host_ui_->is_closing()) {
        return;
      }
      if (host_ui_ && host_ui_->is_open()) {
        host_ui_->Leave();
      } else {
        OpenGraphicsPage();
      }
    });
  }

  void OnShutdown() override {
    rex::ui::UnregisterBind("bind_game_menu");
    fm4_static_recomp::SetInputTick(nullptr);
    fm4_static_recomp::SetPromptTextSource(nullptr);
    fm4_static_recomp::SetGraphicsPageOpener(nullptr);
    if (auto* input_system =
            static_cast<rex::input::InputSystem*>(runtime()->input_system())) {
      input_system->SetGuestStateReadCallbacks({}, {});
    }
    host_ui_.reset();
  }

  // A registered dialog, even one with nothing to draw, moves presentation to
  // the UI thread, which repaints at the display rate on top of every guest
  // frame: ~290 presents of 120 frames, and a present held back (an external
  // frame limiter) no longer throttles the guest.
  std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog()
      override {
    return nullptr;
  }

 private:
  // The keyboard's bindings follow the input context each tick, and its
  // steering, pedals and look are smoothed while driving.
  void InstallKeyboard() {
    namespace input = fm4_static_recomp::input;
    auto* const input_system = static_cast<rex::input::InputSystem*>(runtime()->input_system());
    binding_switcher_ = std::make_unique<input::BindingSwitcher>(controls_);
    // Before the main loop's first tick, so the driver never reads its own keybind_* defaults.
    binding_switcher_->Update(input::InputContext::kMenu);
    prompt_swap_ = CreatePromptSwap();
    fm4_static_recomp::SetInputTick([this, input_system] {
      const input::InputContext context = input::CurrentInputContext();
      binding_switcher_->Update(context);
      if (!input_system) {
        return;
      }
      // The keyboard is a synthetic device; a pad that has not moved never takes over.
      const auto device = input_system->ActiveDevice(0);
      const bool keyboard = device && device->synthetic;
      keyboard_prompts_.store(keyboard, std::memory_order_relaxed);
      if (prompt_swap_) {
        prompt_swap_->Update(keyboard, context);
      }
    });
    fm4_static_recomp::SetPromptTextSource(
        [this](uint16_t key, std::u16string_view text) -> std::optional<std::u16string> {
          if (!keyboard_prompts_.load(std::memory_order_relaxed)) {
            return std::nullopt;
          }
          return input::KeyboardText(key, text, controls_);
        });
    if (!input_system) {
      return;
    }
    input_system->SetGuestStateReadCallbacks(rex::ui::vulkan::Fm4ReflexBeforeInputSample,
                                             rex::ui::vulkan::Fm4ReflexAfterInputSample);
    input_system->SetKeyboardStateFilter(
        [ramp = input::KeyboardRamp(controls_.ramp), last = std::chrono::steady_clock::now()](
            uint32_t user_index, rex::input::X_INPUT_GAMEPAD& pad) mutable {
          if (user_index != 0) {
            return;
          }
          const auto now = std::chrono::steady_clock::now();
          const double elapsed = std::chrono::duration<double>(now - last).count();
          last = now;
          ramp.Apply(pad, elapsed, input::CurrentInputContext() == input::InputContext::kDriving);
        });
  }

  // The keycaps for the game's button icons, from its own icons and font.
  std::unique_ptr<fm4_static_recomp::input::PromptSwap> CreatePromptSwap() {
    namespace hostui = fm4_static_recomp::hostui;
    const auto ui_root = game_data_root() / "Media" / "UI";
    const auto textures = hostui::UiArchive::Open(ui_root / "Textures.zip");
    const auto fonts = hostui::UiArchive::Open(ui_root / "Fonts.zip");
    // The help bar's font (fontmap.xml: B_Swiss_721_Medium).
    const auto font_data = fonts ? fonts->Read("b_vector_aa.dt") : std::nullopt;
    const auto font = font_data ? hostui::VectorFont::Parse(*font_data) : std::nullopt;
    if (!textures || !font) {
      REXLOG_WARN("Prompt icons: cannot read Textures.zip or Fonts.zip; keeping the pad icons");
      return nullptr;
    }
    const auto started = std::chrono::steady_clock::now();
    auto icons = fm4_static_recomp::input::LoadPromptTextures(*textures);
    const size_t count = icons.size();
    auto swap = std::make_unique<fm4_static_recomp::input::PromptSwap>(std::move(icons), *font,
                                                                       controls_);
    REXLOG_INFO("Prompt icons: {} of {} icons, keycaps drawn in {} ms", count,
                fm4_static_recomp::input::kPromptTextureNames.size(),
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count());
    return swap;
  }

  // Returning false makes the process exit with a failure code.
  bool RunShaderCacheTool() {
    namespace shader_cache = fm4_static_recomp::shader_cache;
    const std::span<const uint8_t> image(
        runtime()->memory()->TranslateVirtual<const uint8_t*>(PPCImageConfig.image_base),
        PPCImageConfig.image_size);
    size_t files = 0;
    std::string error;
    const auto scanned = shader_cache::ScanDisc(game_data_root(), image, &error, &files);
    if (!scanned) {
      REXLOG_ERROR("Shader cache: {}", error);
      return false;
    }
    const std::vector<shader_cache::DiscShader>& shaders = *scanned;
    const size_t pixel = size_t(std::count_if(shaders.begin(), shaders.end(),
                                              [](const auto& shader) { return shader.pixel; }));
    REXLOG_INFO("Disc shaders: {} distinct ({} vertex, {} pixel) from {} files", shaders.size(),
                shaders.size() - pixel, pixel, files);
    const std::string dump = REXCVAR_GET(fm4_dump_disc_shaders);
    if (!dump.empty() && !shader_cache::WriteDump(shaders, dump)) {
      REXLOG_ERROR("Disc shaders: cannot write the dump to {}", dump);
      return false;
    }
    const std::string shareable = REXCVAR_GET(fm4_build_shader_cache);
    if (shareable.empty()) {
      return true;
    }
    const auto result = shader_cache::WriteShaderCache(
        shaders, std::string(REXCVAR_GET(fm4_shader_list_dir)), shareable, &error);
    if (!result) {
      REXLOG_ERROR("Shader cache: {}", error);
      return false;
    }
    if (!result->shaders_written && !result->pipelines_added && !result->pipelines_skipped) {
      REXLOG_INFO("Shader cache: already complete, nothing to add ({} of {} shaders, {} not on the "
                  "disc; {} pipelines)",
                  result->shaders_needed - result->shaders_missing, result->shaders_needed,
                  result->shaders_missing, result->pipelines_listed);
      return true;
    }
    const std::string pipelines =
        result->cache_version
            ? fmt::format("pipelines skipped: list version {:08X}, cache version {:08X}",
                          result->list_version, result->cache_version)
        : result->pipelines_skipped
            ? fmt::format("pipelines skipped: list version {:08X}, tool {:08X}",
                          result->list_version, shader_cache::kPipelineStorageVersion)
            : fmt::format("{} of {} pipelines added", result->pipelines_added,
                          result->pipelines_listed);
    REXLOG_INFO("Shader cache: {} of {} shaders ({} not on the disc), {}", result->shaders_written,
                result->shaders_needed, result->shaders_missing, pipelines);
    return true;
  }

  void OpenGraphicsPage() {
    if (host_ui_ && host_ui_->is_open()) {
      return;
    }
    rex::ui::Presenter* const presenter =
        runtime() && runtime()->graphics_system() ? runtime()->graphics_system()->presenter()
                                                  : nullptr;
    if (!presenter || !immediate_drawer() || !window() || !graphics_config_) {
      REXLOG_WARN("Graphics page: presentation is not ready");
      return;
    }
    if (!host_ui_) {
      host_ui_ = std::make_unique<fm4_static_recomp::hostui::HostUi>(
          *this, *presenter, *immediate_drawer(), *window(),
          static_cast<rex::input::InputSystem*>(runtime()->input_system()), game_data_root(),
          controls_.menu.keys[size_t(fm4_static_recomp::input::PadControl::kY)]);
    }
    fm4_static_recomp::ui::GraphicsPageServices services;
    services.display_refresh_hz = [hz = fm4_static_recomp::ui::DisplayRefreshHz(
                                       window()->GetNativeWindowHandle())] { return hz; };
    services.draw_resolution_scale = [this]() -> uint32_t {
      auto* graphics = runtime() ? runtime()->graphics_system() : nullptr;
      return graphics ? graphics->draw_resolution_scale() : 0;
    };
    services.output_size = [this]() -> std::optional<std::pair<uint32_t, uint32_t>> {
      const uint32_t width = window() ? window()->GetActualPhysicalWidth() : 0;
      const uint32_t height = window() ? window()->GetActualPhysicalHeight() : 0;
      if (!width || !height) {
        return std::nullopt;
      }
      if (!REXCVAR_GET(present_letterbox)) {
        return std::pair{width, height};
      }
      return uint64_t(width) * 9 > uint64_t(height) * 16 ? std::pair{height * 16 / 9, height}
                                                         : std::pair{width, width * 9 / 16};
    };
    services.upscalers = [presenter] { return presenter->GetUpscalers(); };
    host_ui_->Open(
        fm4_static_recomp::ui::CreateGraphicsPage(*graphics_config_, std::move(services)));
  }

  std::unique_ptr<fm4_static_recomp::config::HostConfig> graphics_config_;
  std::unique_ptr<fm4_static_recomp::config::HostConfig> controls_config_;
  fm4_static_recomp::input::Controls controls_ = fm4_static_recomp::input::DefaultControls();
  std::unique_ptr<fm4_static_recomp::input::BindingSwitcher> binding_switcher_;
  std::unique_ptr<fm4_static_recomp::input::PromptSwap> prompt_swap_;
  std::atomic<bool> keyboard_prompts_{false};
  std::unique_ptr<fm4_static_recomp::hostui::HostUi> host_ui_;
};
