#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/ui/upscaler.h>

#include "ui/graphics_page.h"

// The GPU's and the game hooks' settings, which this binary does not link.
REXCVAR_DEFINE_INT32(anisotropic_override, 3, "GPU", "Test stand-in");
REXCVAR_DEFINE_INT32(resolution_scale, 1, "GPU", "Test stand-in");
REXCVAR_DEFINE_INT32(fm4_frame_rate, 120, "Forza Motorsport 4", "Test stand-in");
REXCVAR_DEFINE_STRING(swap_post_effect, "none", "GPU", "Test stand-in");
REXCVAR_DEFINE_STRING(fm4_showroom_msaa, "4x", "Forza Motorsport 4", "Test stand-in");
REXCVAR_DEFINE_STRING(fm4_race_msaa, "2x", "Forza Motorsport 4", "Test stand-in");

namespace {

namespace config = fm4_static_recomp::config;
namespace ui = fm4_static_recomp::ui;
using fm4_static_recomp::hostui::MenuRow;
using fm4_static_recomp::hostui::MenuScreen;
using fm4_static_recomp::hostui::NavCommand;
using fm4_static_recomp::hostui::PadNavigator;

std::filesystem::path TempFile(const char* name) {
  return std::filesystem::temp_directory_path() / ("fm4_graphics_page_test_" + std::string(name));
}

rex::ui::UpscalerInfo Upscaler(const char* id, bool available, const char* reason = "") {
  rex::ui::UpscalerInfo info;
  info.id = id;
  info.kind = rex::ui::UpscalerKind::kTemporal;
  info.available = available;
  info.unavailable_reason = reason;
  return info;
}

// FSR 2 and 3 run, DLSS does not: the page on an AMD GPU.
ui::GraphicsPageServices Services() {
  ui::GraphicsPageServices services;
  services.draw_resolution_scale = [] { return 1u; };
  services.output_size = [] { return std::optional(std::pair{2560u, 1440u}); };
  services.upscalers = [] {
    return std::vector{Upscaler("dlss", false, "needs an NVIDIA RTX GPU"), Upscaler("fsr3", true),
                       Upscaler("fsr2", true)};
  };
  return services;
}

// Every upscaler runs: the page on an NVIDIA RTX GPU.
ui::GraphicsPageServices NvidiaServices() {
  auto services = Services();
  services.upscalers = [] {
    return std::vector{Upscaler("dlss", true), Upscaler("fsr3", true), Upscaler("fsr2", true)};
  };
  return services;
}

const MenuRow* Find(const MenuScreen& screen, const char* label) {
  const auto& rows = screen.rows();
  const auto it = std::find_if(rows.begin(), rows.end(),
                               [label](const MenuRow& row) { return row.label == label; });
  return it == rows.end() ? nullptr : &*it;
}

// The cvars are the runtime's, shared by every test: each test starts from no upscaler.
struct Upscaling {
  Upscaling(const char* effect, bool active) {
    rex::cvar::SetFlagByName("present_effect", effect);
    rex::ui::SetTemporalUpscalerActive(active);
  }
  ~Upscaling() {
    rex::cvar::SetFlagByName("present_effect", "bilinear");
    rex::ui::SetTemporalUpscalerActive(false);
  }
};

// Puts the named cvars back as they were.
struct CvarsRestored {
  explicit CvarsRestored(std::vector<std::string> names) : names(std::move(names)) {
    for (const std::string& name : this->names) {
      values.push_back(rex::cvar::GetFlagByName(name));
    }
  }
  ~CvarsRestored() {
    for (size_t i = 0; i < names.size(); ++i) {
      rex::cvar::SetFlagByName(names[i], values[i]);
    }
  }
  std::vector<std::string> names;
  std::vector<std::string> values;
};

// Steps the row until it shows `label`.
void Choose(const MenuRow& row, const std::string& label) {
  for (int i = 0; i < 16 && row.value() != label; ++i) {
    row.adjust(+1);
  }
  REQUIRE(row.value() == label);
}

// The label of the row the focus reaches from `from` by `command`.
std::string FocusAfter(MenuScreen& page, const char* from, NavCommand command) {
  const auto& rows = page.rows();
  const auto it = std::find_if(rows.begin(), rows.end(),
                               [from](const MenuRow& row) { return row.label == from; });
  REQUIRE(page.SetFocus(size_t(it - rows.begin())));
  page.Handle(command);
  return rows[page.focus()].label;
}

// Runs the page's per-frame update while it applies, at most `frames` times.
void Settle(MenuScreen& page, int frames = 64) {
  for (int i = 0; i < frames && page.status().busy; ++i) {
    page.Update(false);
  }
}

}  // namespace

TEST_CASE("The launcher's schema offers the upscaler, its sharpness and the DLSS preset while DLSS "
          "is chosen",
          "[graphics_page]") {
  const auto path = TempFile("schema.json");
  REQUIRE(ui::WriteGraphicsSchema(path));
  std::stringstream text;
  text << std::ifstream(path).rdbuf();
  const std::string schema = text.str();
  const auto at = [&schema](const char* label) {
    return schema.find(std::string("{\"label\":\"") + label + "\"");
  };
  REQUIRE(at("FREE ASPECT RATIO") != std::string::npos);
  REQUIRE(at("UPSCALER") != std::string::npos);
  REQUIRE(at("DLSS PRESET") != std::string::npos);
  REQUIRE(at("SHARPNESS") != std::string::npos);
  CHECK(at("VSYNC") < at("FREE ASPECT RATIO"));
  CHECK(at("FREE ASPECT RATIO") < at("RESOLUTION SCALE"));
  CHECK(at("RESOLUTION SCALE") < at("UPSCALER"));
  CHECK(at("UPSCALER") < at("DLSS PRESET"));
  CHECK(at("DLSS PRESET") < at("SHARPNESS"));
  CHECK(at("SHARPNESS") < at("GRAPHICS PRESET"));
  CHECK(schema.find("\"key\":\"present_effect\"") != std::string::npos);
  CHECK(schema.find("\"key\":\"present_free_aspect\"") != std::string::npos);
  CHECK(schema.find("{\"label\":\"OFF\",\"value\":\"bilinear\"}") != std::string::npos);
  CHECK(schema.find("{\"label\":\"30 %\",\"value\":\"30\"}") != std::string::npos);
  const std::string preset = schema.substr(at("DLSS PRESET"), at("SHARPNESS") - at("DLSS PRESET"));
  CHECK(preset.find("\"key\":\"present_dlss_preset\"") != std::string::npos);
  CHECK(preset.find("\"default\":\"auto\"") != std::string::npos);
  CHECK(preset.find("\"shown_when\":{\"present_effect\":\"dlss\"}") != std::string::npos);
  CHECK(schema.find("shown_when") == schema.rfind("shown_when"));
  std::filesystem::remove(path);
}

TEST_CASE("The in-game page has UPSCALER and SHARPNESS and leaves the DLSS preset to the launcher",
          "[graphics_page]") {
  config::HostConfig config(TempFile("rows.toml"));
  const auto page = ui::CreateGraphicsPage(config, Services());
  CHECK(Find(*page, "UPSCALER"));
  CHECK(Find(*page, "SHARPNESS"));
  CHECK(Find(*page, "FREE ASPECT RATIO"));
  CHECK_FALSE(Find(*page, "DLSS PRESET"));
}

TEST_CASE("FREE ASPECT RATIO is saved and applied", "[graphics_page]") {
  const auto path = TempFile("free_aspect.toml");
  std::filesystem::remove(path);
  config::HostConfig config(path);
  const CvarsRestored restored({"present_free_aspect"});
  REQUIRE(rex::cvar::SetFlagByName("present_free_aspect", "true"));
  const auto page = ui::CreateGraphicsPage(config, Services());
  const MenuRow& row = *Find(*page, "FREE ASPECT RATIO");
  CHECK(row.value() == "ON");
  row.adjust(+1);
  CHECK(row.value() == "OFF");
  CHECK(row.unapplied());
  page->Handle(NavCommand::kApply);
  page->Update(false);
  CHECK(rex::cvar::GetFlagByName("present_free_aspect") == "false");
  CHECK(config.Get("present_free_aspect").value_or("") == "false");
  std::filesystem::remove(path);
}

TEST_CASE("FREE ASPECT RATIO reports the whole surface and OFF reports the 16:9 area",
          "[graphics_page]") {
  CHECK(ui::PresentedOutputSize(2560, 1080, true, true) == std::pair{2560u, 1080u});
  CHECK(ui::PresentedOutputSize(2560, 1080, false, true) == std::pair{1920u, 1080u});
  CHECK(ui::PresentedOutputSize(1080, 1920, true, true) == std::pair{1080u, 1920u});
  CHECK(ui::PresentedOutputSize(1080, 1920, false, true) == std::pair{1080u, 607u});
  CHECK(ui::PresentedOutputSize(2560, 1080, false, false) == std::pair{2560u, 1080u});
}

TEST_CASE("UPSCALER steps past an upscaler the GPU cannot run", "[graphics_page]") {
  const auto path = TempFile("step.toml");
  std::filesystem::remove(path);
  config::HostConfig config(path);
  const Upscaling upscaling("bilinear", false);
  const auto page = ui::CreateGraphicsPage(config, Services());
  const MenuRow& row = *Find(*page, "UPSCALER");
  CHECK(row.value() == "OFF");
  row.adjust(+1);
  CHECK(row.value() == "FSR 2");
  row.adjust(+1);
  CHECK(row.value() == "FSR 3");
  row.adjust(+1);
  CHECK(row.value() == "OFF");
  row.adjust(-1);
  CHECK(row.value() == "FSR 3");
  CHECK(rex::cvar::GetFlagByName("present_effect") == "bilinear");
  page->Handle(NavCommand::kApply);
  CHECK(config.Get("present_effect").value_or("") == "fsr3");
  Settle(*page);
  CHECK(rex::cvar::GetFlagByName("present_effect") == "fsr3");
  std::filesystem::remove(path);
}

TEST_CASE("A change waits for SAVE & APPLY, then the page says it is applied", "[graphics_page]") {
  const auto path = TempFile("draft.toml");
  std::filesystem::remove(path);
  config::HostConfig config(path);
  const CvarsRestored restored({"anisotropic_override"});
  rex::cvar::SetFlagByName("anisotropic_override", "-1");
  const auto page = ui::CreateGraphicsPage(config, Services());
  const MenuRow& row = *Find(*page, "ANISOTROPIC FILTERING");
  CHECK(page->status().text.empty());
  row.adjust(+1);
  CHECK(row.value() == "2X");
  CHECK(row.unapplied());
  CHECK(rex::cvar::GetFlagByName("anisotropic_override") == "-1");
  CHECK(page->status().text == "UNSAVED CHANGES");
  row.adjust(-1);
  CHECK_FALSE(row.unapplied());
  CHECK(page->status().text.empty());
  row.adjust(+1);
  page->Handle(NavCommand::kApply);
  CHECK(config.Get("anisotropic_override").value_or("") == "2");
  CHECK(page->status().busy);
  Settle(*page);
  CHECK(rex::cvar::GetFlagByName("anisotropic_override") == "2");
  CHECK_FALSE(row.unapplied());
  CHECK(page->status().text == "SETTINGS APPLIED. YOU CAN CONTINUE.");
  row.adjust(+1);
  CHECK(page->status().text == "UNSAVED CHANGES");
  std::filesystem::remove(path);
}

TEST_CASE("Leaving with unapplied changes warns once, then discards them", "[graphics_page]") {
  config::HostConfig config(TempFile("leave.toml"));
  const CvarsRestored restored({"anisotropic_override"});
  rex::cvar::SetFlagByName("anisotropic_override", "-1");
  const auto page = ui::CreateGraphicsPage(config, Services());
  CHECK(page->Handle(NavCommand::kBack) == MenuScreen::Result::kBack);
  Find(*page, "ANISOTROPIC FILTERING")->adjust(+1);
  CHECK(page->Handle(NavCommand::kClose) == MenuScreen::Result::kChanged);
  CHECK(page->status().warning);
  // A further change asks again.
  Find(*page, "ANISOTROPIC FILTERING")->adjust(+1);
  CHECK_FALSE(page->status().warning);
  CHECK(page->Handle(NavCommand::kBack) == MenuScreen::Result::kChanged);
  CHECK(page->Handle(NavCommand::kBack) == MenuScreen::Result::kBack);
  page->Update(true);
  CHECK(rex::cvar::GetFlagByName("anisotropic_override") == "-1");
}

TEST_CASE("SAVE & APPLY sets the settings one at a time, the rest after the renderer takes the "
          "scale",
          "[graphics_page]") {
  config::HostConfig config(TempFile("sequence.toml"));
  const CvarsRestored restored({"resolution_scale", "anisotropic_override", "fm4_frame_rate"});
  rex::cvar::SetFlagByName("resolution_scale", "1");
  rex::cvar::SetFlagByName("anisotropic_override", "-1");
  rex::cvar::SetFlagByName("fm4_frame_rate", "60");
  auto renderer_scale = std::make_shared<uint32_t>(1);
  auto services = Services();
  services.draw_resolution_scale = [renderer_scale] { return *renderer_scale; };
  const auto page = ui::CreateGraphicsPage(config, services);
  Find(*page, "ANISOTROPIC FILTERING")->adjust(+1);
  Find(*page, "RESOLUTION SCALE")->adjust(+1);
  page->SetFocus(0);
  CHECK(page->Slide(+12) == MenuScreen::Result::kChanged);
  CHECK(Find(*page, "FRAME RATE")->unapplied());
  CHECK(rex::cvar::GetFlagByName("fm4_frame_rate") == "60");
  page->Handle(NavCommand::kApply);
  page->Update(false);
  CHECK(rex::cvar::GetFlagByName("resolution_scale") == "2");
  // Until the renderer draws at 2x nothing else is set, no row changes and the page stays.
  for (int i = 0; i < 8; ++i) {
    page->Update(false);
  }
  CHECK(rex::cvar::GetFlagByName("fm4_frame_rate") == "60");
  CHECK(rex::cvar::GetFlagByName("anisotropic_override") == "-1");
  CHECK(page->status().busy);
  CHECK(page->Handle(NavCommand::kBack) == MenuScreen::Result::kChanged);
  CHECK(page->Handle(NavCommand::kRight) == MenuScreen::Result::kNone);
  CHECK(page->status().text == "APPLYING SETTINGS");
  // A switch this long is the driver building the scale's pipelines.
  std::this_thread::sleep_for(std::chrono::milliseconds(3100));
  page->Update(false);
  CHECK(page->status().text == "BUILDING SHADERS FOR 2X, FIRST TIME ONLY");
  CHECK(page->status().busy);
  *renderer_scale = 2;
  page->Update(false);
  CHECK(rex::cvar::GetFlagByName("fm4_frame_rate") == "72");
  CHECK(rex::cvar::GetFlagByName("anisotropic_override") == "-1");
  page->Update(false);
  CHECK(rex::cvar::GetFlagByName("anisotropic_override") == "2");
  CHECK_FALSE(page->status().busy);
  CHECK(page->Handle(NavCommand::kBack) == MenuScreen::Result::kBack);
}

TEST_CASE("A drafted upscaler already takes over the anti-aliasing rows", "[graphics_page]") {
  config::HostConfig config(TempFile("drafted_upscaler.toml"));
  const Upscaling upscaling("bilinear", false);
  const auto page = ui::CreateGraphicsPage(config, Services());
  Find(*page, "UPSCALER")->adjust(-1);
  CHECK(Find(*page, "UPSCALER")->value() == "FSR 3");
  CHECK(Find(*page, "POST AA")->value() == "OFF (FSR 3)");
  CHECK_FALSE(Find(*page, "RACE MSAA")->is_enabled());
  CHECK(Find(*page, "SHARPNESS")->is_enabled());
  CHECK(Find(*page, "UPSCALER")->note() ==
        "Renders 1280x720, upscaled to 2560x1440 by FSR 3 (2.0x).");
}

TEST_CASE("Y applies, and START does not close while BACK is held for BACK+START",
          "[graphics_page]") {
  PadNavigator pad;
  pad.Poll(0, 0, 0, 0);
  CHECK(pad.Poll(PadNavigator::kButtonY, 0, 0, 10) == std::vector{NavCommand::kApply});
  CHECK(pad.Poll(PadNavigator::kBack, 0, 0, 20).empty());
  CHECK(pad.Poll(PadNavigator::kBack | PadNavigator::kStart, 0, 0, 30).empty());
  CHECK(pad.Poll(0, 0, 0, 40).empty());
  CHECK(pad.Poll(PadNavigator::kStart, 0, 0, 50) == std::vector{NavCommand::kClose});
}

TEST_CASE("UPSCALER says why a chosen upscaler does not run", "[graphics_page]") {
  config::HostConfig config(TempFile("reason.toml"));
  const Upscaling upscaling("dlss", false);
  const auto page = ui::CreateGraphicsPage(config, Services());
  const MenuRow& row = *Find(*page, "UPSCALER");
  CHECK(row.value() == "DLSS");
  CHECK(row.note() == "DLSS does not run here: needs an NVIDIA RTX GPU.");
}

TEST_CASE("While an upscaler runs, the notes give the render size, output size and factor",
          "[graphics_page]") {
  config::HostConfig config(TempFile("note.toml"));
  const Upscaling upscaling("fsr3", true);
  const auto page = ui::CreateGraphicsPage(config, Services());
  const std::string line = "Renders 1280x720, upscaled to 2560x1440 by FSR 3 (2.0x).";
  CHECK(Find(*page, "UPSCALER")->note() == line);
  CHECK(Find(*page, "RESOLUTION SCALE")->note() == line);
}

TEST_CASE("At the output size the notes name the upscaler's anti-aliasing mode", "[graphics_page]") {
  config::HostConfig config(TempFile("native.toml"));
  auto services = Services();
  services.output_size = [] { return std::optional(std::pair{1280u, 720u}); };
  {
    const Upscaling upscaling("dlss", true);
    const auto page = ui::CreateGraphicsPage(config, services);
    CHECK(Find(*page, "RESOLUTION SCALE")->note() == "Renders 1280x720, shown at that size with DLAA.");
  }
  const Upscaling upscaling("fsr3", true);
  const auto page = ui::CreateGraphicsPage(config, services);
  const std::string line = "Renders 1280x720, shown at that size with FSR 3 Native AA.";
  CHECK(Find(*page, "UPSCALER")->note() == line);
  CHECK(Find(*page, "RESOLUTION SCALE")->note() == line);
}

TEST_CASE("Without an upscaler the notes keep the plain stretch", "[graphics_page]") {
  config::HostConfig config(TempFile("stretch.toml"));
  const Upscaling upscaling("bilinear", false);
  const auto page = ui::CreateGraphicsPage(config, Services());
  CHECK(Find(*page, "RESOLUTION SCALE")->note() == "Renders 1280x720, stretched to 2560x1440.");
  CHECK(Find(*page, "UPSCALER")->note().empty());
}

TEST_CASE("An upscaler takes over POST AA and enables SHARPNESS", "[graphics_page]") {
  config::HostConfig config(TempFile("owned.toml"));
  const auto page = ui::CreateGraphicsPage(config, Services());
  const MenuRow& post_aa = *Find(*page, "POST AA");
  const MenuRow& sharpness = *Find(*page, "SHARPNESS");
  {
    const Upscaling upscaling("bilinear", false);
    CHECK(post_aa.is_enabled());
    CHECK_FALSE(sharpness.is_enabled());
  }
  {
    const Upscaling upscaling("dlss", true);
    CHECK(post_aa.value() == "OFF (DLSS)");
    CHECK_FALSE(post_aa.is_enabled());
    CHECK(sharpness.is_enabled());
    CHECK(sharpness.value() == "30 %");
  }
}

TEST_CASE("Every live and drafted upscaler and scale greys exactly the anti-aliasing rows or "
          "SHARPNESS",
          "[graphics_page]") {
  struct Case {
    const char* live;
    bool active;     // the live upscaler is upscaling
    bool nvidia;     // DLSS runs on this GPU
    const char* drafted;  // the UPSCALER row's drafted choice, or null
    uint32_t scale;       // the scale the renderer draws at
    const char* upscaling;  // the upscaler the rows should follow, or empty
  };
  const Case cases[] = {
      {"bilinear", false, false, nullptr, 1, ""},
      {"bilinear", false, false, "FSR 2", 1, "FSR 2"},
      {"bilinear", false, true, "DLSS", 1, "DLSS"},
      {"fsr3", true, false, nullptr, 1, "FSR 3"},
      {"fsr3", true, false, "OFF", 1, ""},
      {"fsr3", true, false, "FSR 2", 1, "FSR 2"},
      {"dlss", true, true, nullptr, 1, "DLSS"},
      {"dlss", true, true, "OFF", 1, ""},
      {"dlss", true, true, "FSR 3", 1, "FSR 3"},
      // Chosen in the file, but this GPU cannot run it, so nothing upscales.
      {"dlss", false, false, nullptr, 1, ""},
      {"dlss", false, false, "FSR 2", 1, "FSR 2"},
      // Chosen, but its runtime failed to start.
      {"fsr3", false, false, nullptr, 1, ""},
      // The output is 2560x1440: at 2x the upscaler runs at 1:1. A larger render (3x,
      // saved with the upscaler in the launcher) is downscaled without one, and choosing
      // an upscaler lowers the scale to 2x.
      {"bilinear", false, false, "FSR 2", 2, "FSR 2"},
      {"dlss", true, true, nullptr, 2, "DLSS"},
      {"fsr3", false, false, nullptr, 3, ""},
      {"bilinear", false, false, "FSR 2", 3, "FSR 2"},
      {"dlss", false, true, "FSR 3", 3, "FSR 3"},
  };
  for (const Case& c : cases) {
    CAPTURE(c.live, c.active, c.nvidia, c.drafted ? c.drafted : "-", c.scale);
    config::HostConfig config(TempFile("greying.toml"));
    const Upscaling live(c.live, c.active);
    const CvarsRestored restored(
        {"swap_post_effect", "fm4_showroom_msaa", "fm4_race_msaa", "resolution_scale"});
    rex::cvar::SetFlagByName("resolution_scale", std::to_string(c.scale));
    rex::cvar::SetFlagByName("swap_post_effect", "fxaa");
    rex::cvar::SetFlagByName("fm4_showroom_msaa", "2x");
    rex::cvar::SetFlagByName("fm4_race_msaa", "4x");
    auto services = c.nvidia ? NvidiaServices() : Services();
    services.draw_resolution_scale = [scale = c.scale] { return scale; };
    const auto page = ui::CreateGraphicsPage(config, services);
    if (c.drafted) {
      Choose(*Find(*page, "UPSCALER"), c.drafted);
    }
    const std::string upscaling = c.upscaling;
    const bool upscales = !upscaling.empty();
    CHECK(Find(*page, "SHARPNESS")->is_enabled() == upscales);
    CHECK(Find(*page, "UPSCALER")->is_enabled());
    CHECK(Find(*page, "RESOLUTION SCALE")->is_enabled());
    CHECK(Find(*page, "GRAPHICS PRESET")->is_enabled());
    for (const auto& [label, own] : {std::pair{"SHOWROOM MSAA", "2X"}, std::pair{"RACE MSAA", "4X"},
                                     std::pair{"POST AA", "FXAA"}}) {
      CAPTURE(label);
      const MenuRow& row = *Find(*page, label);
      CHECK(row.is_enabled() == !upscales);
      CHECK(row.value() == (upscales ? "OFF (" + upscaling + ")" : std::string(own)));
    }
    // The focus steps over the greyed rows.
    CHECK(FocusAfter(*page, "UPSCALER", NavCommand::kDown) ==
          (upscales ? "SHARPNESS" : "GRAPHICS PRESET"));
    CHECK(FocusAfter(*page, "GRAPHICS PRESET", NavCommand::kDown) ==
          (upscales ? "ANISOTROPIC FILTERING" : "SHOWROOM MSAA"));
    CHECK(FocusAfter(*page, "CROWDS", NavCommand::kDown) == (upscales ? "BLOOM" : "POST AA"));
    CHECK(FocusAfter(*page, "GRAPHICS PRESET", NavCommand::kUp) ==
          (upscales ? "SHARPNESS" : "UPSCALER"));
    const std::string note = Find(*page, "RESOLUTION SCALE")->note();
    CAPTURE(note);
    if (upscales) {
      // DLSS at 1:1 goes by DLAA.
      CHECK((note.find(upscaling) != std::string::npos ||
             (upscaling == "DLSS" && note.find("DLAA") != std::string::npos)));
    } else {
      CHECK(note.find("upscaled to") == std::string::npos);
      CHECK(note.find("shown at that size with") == std::string::npos);
    }
  }
}

TEST_CASE("A preset chosen while an upscaler greys the anti-aliasing rows sets them for later",
          "[graphics_page]") {
  config::HostConfig config(TempFile("greyed_preset.toml"));
  const Upscaling upscaling("bilinear", false);
  const CvarsRestored restored({"swap_post_effect", "fm4_showroom_msaa", "fm4_race_msaa"});
  rex::cvar::SetFlagByName("swap_post_effect", "none");
  rex::cvar::SetFlagByName("fm4_showroom_msaa", "4x");
  rex::cvar::SetFlagByName("fm4_race_msaa", "2x");
  const auto page = ui::CreateGraphicsPage(config, Services());
  const MenuRow& upscaler = *Find(*page, "UPSCALER");
  Choose(upscaler, "FSR 3");
  Choose(*Find(*page, "GRAPHICS PRESET"), "EXPERIMENTAL");
  CHECK(Find(*page, "POST AA")->value() == "OFF (FSR 3)");
  CHECK(Find(*page, "RACE MSAA")->unapplied());
  Choose(upscaler, "OFF");
  CHECK(Find(*page, "POST AA")->value() == "FXAA EXTREME");
  CHECK(Find(*page, "SHOWROOM MSAA")->value() == "4X");
  CHECK(Find(*page, "RACE MSAA")->value() == "4X");
  CHECK(Find(*page, "GRAPHICS PRESET")->value() == "EXPERIMENTAL");
  page->Handle(NavCommand::kApply);
  Settle(*page);
  CHECK(rex::cvar::GetFlagByName("swap_post_effect") == "fxaa_extreme");
  CHECK(rex::cvar::GetFlagByName("fm4_race_msaa") == "4x");
  CHECK(rex::cvar::GetFlagByName("present_effect") == "bilinear");
}

TEST_CASE("A greyed row cannot take the focus, so neither the pad nor the mouse changes it", "[graphics_page]") {
  config::HostConfig config(TempFile("greyed_input.toml"));
  const Upscaling upscaling("fsr3", true);
  const auto page = ui::CreateGraphicsPage(config, Services());
  const auto& rows = page->rows();
  const size_t race_msaa = size_t(std::find_if(rows.begin(), rows.end(), [](const MenuRow& row) {
                                    return row.label == "RACE MSAA";
                                  }) -
                                  rows.begin());
  CHECK_FALSE(page->SetFocus(race_msaa));
  CHECK(page->rows()[page->focus()].label != "RACE MSAA");
  CHECK(page->Handle(NavCommand::kApply) == MenuScreen::Result::kChanged);
  CHECK_FALSE(page->status().busy);
  CHECK(page->status().text.empty());
}

TEST_CASE("A saved upscaler that cannot run at the saved scale says so on its row and the "
          "scale's",
          "[graphics_page]") {
  config::HostConfig config(TempFile("idle_upscaler.toml"));
  const Upscaling upscaling("dlss", false);
  const CvarsRestored restored({"resolution_scale"});
  rex::cvar::SetFlagByName("resolution_scale", "3");
  auto services = NvidiaServices();
  services.draw_resolution_scale = [] { return 3u; };
  const auto page = ui::CreateGraphicsPage(config, services);
  const MenuRow& upscaler = *Find(*page, "UPSCALER");
  const MenuRow& scale = *Find(*page, "RESOLUTION SCALE");
  const std::string idle =
      "DLSS runs up to 2x on this 2560x1440 screen: at 3x the picture is downscaled without it, "
      "with MSAA and post AA instead.";
  CHECK(upscaler.value() == "DLSS (OFF AT 3X)");
  CHECK(upscaler.note() == idle);
  CHECK(scale.note() == "Renders 3840x2160, downscaled to 2560x1440. " + idle);
  CHECK(Find(*page, "RACE MSAA")->is_enabled());
  CHECK_FALSE(Find(*page, "SHARPNESS")->is_enabled());
  // A screen smaller than 1280x720 leaves the upscaler no scale at all, and limits none.
  rex::cvar::SetFlagByName("resolution_scale", "1");
  services.draw_resolution_scale = [] { return 1u; };
  services.output_size = [] { return std::optional(std::pair{1024u, 576u}); };
  const auto small = ui::CreateGraphicsPage(config, services);
  CHECK(Find(*small, "UPSCALER")->value() == "DLSS (OFF AT 1X)");
  CHECK(Find(*small, "UPSCALER")->note() ==
        "DLSS does not run on this 1024x576 screen: at 1x the picture is downscaled without it, "
        "with MSAA and post AA instead.");
  Choose(*Find(*small, "RESOLUTION SCALE"), "4X");
}

TEST_CASE("With an upscaler chosen, RESOLUTION SCALE offers only the scales that fit the screen",
          "[graphics_page]") {
  config::HostConfig config(TempFile("fitting_scales.toml"));
  const CvarsRestored restored({"resolution_scale"});
  rex::cvar::SetFlagByName("resolution_scale", "3");
  auto services = NvidiaServices();
  services.draw_resolution_scale = [] { return 3u; };
  const auto steps = [](const MenuRow& row, int direction, int count) {
    std::vector<std::string> values;
    for (int i = 0; i < count; ++i) {
      row.adjust(direction);
      values.push_back(row.value());
    }
    return values;
  };
  {
    // Without an upscaler every scale, supersampling included.
    const Upscaling upscaling("bilinear", false);
    const auto page = ui::CreateGraphicsPage(config, services);
    CHECK(steps(*Find(*page, "RESOLUTION SCALE"), +1, 4) ==
          std::vector<std::string>{"4X", "1X", "2X", "3X"});
  }
  {
    // A saved 3x under DLSS steps into the fitting 1x and 2x and stays there.
    const Upscaling upscaling("dlss", false);
    const auto page = ui::CreateGraphicsPage(config, services);
    const MenuRow& scale = *Find(*page, "RESOLUTION SCALE");
    CHECK(steps(scale, +1, 3) == std::vector<std::string>{"1X", "2X", "1X"});
    CHECK(steps(scale, -1, 2) == std::vector<std::string>{"2X", "1X"});
    CHECK(Find(*page, "UPSCALER")->value() == "DLSS");
  }
  {
    // On a 4K screen 3x fits too.
    const Upscaling upscaling("fsr3", true);
    auto uhd = services;
    uhd.output_size = [] { return std::optional(std::pair{3840u, 2160u}); };
    const auto page = ui::CreateGraphicsPage(config, uhd);
    CHECK(steps(*Find(*page, "RESOLUTION SCALE"), +1, 3) ==
          std::vector<std::string>{"1X", "2X", "3X"});
  }
}

TEST_CASE("Choosing an upscaler over a scale above the screen drafts the scale down and says so",
          "[graphics_page]") {
  config::HostConfig config(TempFile("lowered_scale.toml"));
  const Upscaling upscaling("bilinear", false);
  const CvarsRestored restored({"resolution_scale"});
  rex::cvar::SetFlagByName("resolution_scale", "4");
  auto services = NvidiaServices();
  services.draw_resolution_scale = [] { return 4u; };
  const auto page = ui::CreateGraphicsPage(config, services);
  const MenuRow& upscaler = *Find(*page, "UPSCALER");
  const MenuRow& scale = *Find(*page, "RESOLUTION SCALE");
  upscaler.adjust(-1);
  CHECK(upscaler.value() == "DLSS");
  CHECK(scale.value() == "2X");
  CHECK(scale.unapplied());
  CHECK(upscaler.unapplied());
  CHECK(upscaler.note() == "Resolution scale lowered to 2x: DLSS runs up to 2x on this 2560x1440 "
                           "screen.");
  CHECK(Find(*page, "POST AA")->value() == "OFF (DLSS)");
  CHECK(rex::cvar::GetFlagByName("resolution_scale") == "4");
  // The next change drops the note; turning the upscaler off keeps the lowered scale.
  upscaler.adjust(+1);
  CHECK(upscaler.value() == "OFF");
  CHECK(scale.value() == "2X");
  CHECK(upscaler.note().empty());
  // Discarding leaves the saved 4x as it was.
  CHECK(page->Handle(NavCommand::kBack) == MenuScreen::Result::kChanged);
  CHECK(page->Handle(NavCommand::kBack) == MenuScreen::Result::kBack);
  CHECK(rex::cvar::GetFlagByName("resolution_scale") == "4");
}
