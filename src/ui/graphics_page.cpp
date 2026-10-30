#include "ui/graphics_page.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/upscaler.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fm4_static_recomp::ui {
namespace {

using hostui::MenuRow;
using hostui::MenuScreen;

struct Choice {
  const char* label;
  const char* value;
};

struct Setting {
  const char* name;
  bool string;  // written quoted
  std::vector<Choice> choices;  // none for a whole number in [low, high]
  int low = 0;
  int high = 0;
};

const Setting kFrameRate = {"fm4_frame_rate", false, {}, 60, 240};
const Setting kVsync = {"vulkan_present_mode", true, {{"OFF", "immediate"}, {"ON", "fifo"}}};
const Setting kDetail = {"fm4_level_of_detail",
                         true,
                         {{"CONSOLE", "console"},
                          {"HIGH", "high"},
                          {"ULTRA", "ultra"}}};
const Setting kScale = {"resolution_scale", false, {{"1X", "1"}, {"2X", "2"}, {"3X", "3"}, {"4X", "4"}}};
const Setting kUpscaler = {"present_effect",
                           true,
                           {{"OFF", "bilinear"},
                            {"FSR 1", "fsr"},
                            {"FSR 2", "fsr2"},
                            {"FSR 3", "fsr3"},
                            {"DLSS", "dlss"}}};
const Setting kDlssPreset = {
    "present_dlss_preset",
    true,
    {{"AUTO", "auto"}, {"K", "k"}, {"M", "m"}, {"J", "j"}, {"L", "l"}, {"DEFAULT", "default"}}};
const Setting kSharpness = {"present_upscaler_sharpness",
                            false,
                            {{"OFF", "0"},
                             {"10 %", "10"},
                             {"20 %", "20"},
                             {"30 %", "30"},
                             {"40 %", "40"},
                             {"50 %", "50"},
                             {"60 %", "60"},
                             {"70 %", "70"},
                             {"80 %", "80"},
                             {"90 %", "90"},
                             {"100 %", "100"}}};
const Setting kPostAa = {
    "swap_post_effect", true, {{"OFF", "none"}, {"FXAA", "fxaa"}, {"FXAA EXTREME", "fxaa_extreme"}}};
const Setting kShowroomMsaa = {
    "fm4_showroom_msaa", true, {{"4X", "4x"}, {"2X", "2x"}, {"OFF", "off"}}};
const Setting kRaceMsaa = {"fm4_race_msaa", true, {{"4X", "4x"}, {"2X", "2x"}, {"OFF", "off"}}};
const Setting kAnisotropy = {"anisotropic_override",
                             false,
                             {{"GAME", "-1"}, {"2X", "2"}, {"4X", "3"}, {"8X", "4"}, {"16X", "5"}}};

const Setting kMotionBlur = {"fm4_road_detail_blur", false, {{"ON", "true"}, {"OFF", "false"}}};
const Setting kDepthOfField = {"fm4_depth_of_field", false, {{"ON", "true"}, {"OFF", "false"}}};
const Setting kCarDirt = {"fm4_car_dirt", false, {{"ON", "true"}, {"OFF", "false"}}};

const Setting kSceneryShadows = {"fm4_scenery_shadows", false, {{"OFF", "false"}, {"ON", "true"}}};
const Setting kReflectionUpdates = {
    "fm4_reflection_updates",
    true,
    {{"CONSOLE", "console"}, {"EVERY RACE", "every_race"}, {"DOUBLE", "double"}}};
const Setting kMirror = {"fm4_full_rate_mirror", false, {{"CONSOLE", "false"}, {"FULL", "true"}}};
const Setting kBloom = {"fm4_full_rate_bloom", false, {{"CONSOLE", "false"}, {"FULL RATE", "true"}}};
const Setting kSoftParticles = {"fm4_soft_particles", false, {{"OFF", "false"}, {"ON", "true"}}};
const Setting kCrowds = {
    "fm4_crowds_every_race", false, {{"CONSOLE", "false"}, {"EVERY RACE", "true"}}};

// Every setting but the frame rate, the resolution scale and the upscaler's.
const Setting* const kPresetSettings[] = {
    &kPostAa, &kShowroomMsaa, &kRaceMsaa, &kAnisotropy, &kSceneryShadows, &kReflectionUpdates,
    &kDetail, &kMirror, &kBloom, &kSoftParticles, &kCrowds, &kMotionBlur, &kDepthOfField,
    &kCarDirt};
constexpr size_t kPresetSettingCount = std::size(kPresetSettings);

const Setting* const kSettings[] = {
    &kFrameRate, &kVsync, &kScale, &kUpscaler, &kDlssPreset, &kSharpness, &kPostAa,
    &kShowroomMsaa, &kRaceMsaa, &kAnisotropy,
    &kSceneryShadows, &kReflectionUpdates, &kDetail, &kMirror, &kBloom, &kSoftParticles, &kCrowds,
    &kMotionBlur, &kDepthOfField, &kCarDirt};

struct Preset {
  const char* label;
  std::array<const char*, kPresetSettingCount> values;  // in kPresetSettings' order
};

// The page shows the preset all settings match, or CUSTOM.
const Preset kPresets[] = {
    {"CONSOLE",
     {"none", "4x", "2x", "-1", "false", "console", "console", "false", "false", "false",
      "false", "true", "true", "true"}},
    {"HIGH",
     {"fxaa", "2x", "2x", "5", "false", "console", "high", "false", "true", "false",
      "false", "true", "true", "true"}},
    {"ULTRA",
     {"fxaa_extreme", "2x", "2x", "5", "false", "every_race", "ultra", "true", "true",
      "true", "false", "true", "true", "true"}},
    {"EXPERIMENTAL",
     {"fxaa_extreme", "4x", "4x", "5", "true", "double", "ultra", "true", "true", "true",
      "true", "true", "true", "true"}},
};
constexpr int kPresetCount = int(std::size(kPresets));

struct RowText {
  const char* label;
  const Setting* setting;  // null for the preset row
  const char* description;
  bool divider_above = false;
  // The launcher shows the row only while this setting has this value.
  const Setting* shown_with = nullptr;
  const char* shown_value = nullptr;
};

// The page's rows in order; the frame rate, scale, upscaler and preset rows
// add behaviour of their own in CreateGraphicsPage. The DLSS preset is
// the launcher's alone: DLSS takes it only as it starts.
const RowText kRows[] = {
    {"FRAME RATE", &kFrameRate,
     "Frames per second in races, menus and the showroom, from the console's 60 up to your "
     "display's refresh rate. A rate the GPU cannot hold stutters: lower it or the resolution "
     "scale."},
    {"VSYNC", &kVsync,
     "On stops tearing on a display without G-Sync or FreeSync. With either, leave it off: "
     "on, an Nvidia GPU shows frames through the desktop, without variable refresh and at a "
     "lower frame rate."},
    {"RESOLUTION SCALE", &kScale,
     "Renders the game at a multiple of 1280x720: sharper edges and textures, more GPU load."},
    {"UPSCALER", &kUpscaler,
     "Upscales the picture from the resolution scale to your display. DLSS (NVIDIA RTX GPUs), "
     "FSR 2 and FSR 3 (any GPU) use the game's depth and motion and smooth edges themselves: "
     "while one runs, MSAA and post AA are off, the MSAA change from the next screen. FSR 1 "
     "(any GPU, light) upscales and sharpens the finished picture, after MSAA and post AA. Off "
     "stretches the picture."},
    {"DLSS PRESET", &kDlssPreset,
     "The DLSS model. Auto takes M where DLSS upscales by more than 1.8x on an RTX 40 or later "
     "GPU, else K; Default leaves the choice to the DLSS runtime and the NVIDIA app's override.",
     false, &kUpscaler, "dlss"},
    {"SHARPNESS", &kSharpness,
     "Sharpens the picture after the upscaler. Off leaves the picture of DLSS, FSR 2 or FSR 3 "
     "as it is; FSR 1 always sharpens a little."},
    {"GRAPHICS PRESET", nullptr,
     "Sets every setting below. Console is the game as on the console; "
     "High raises most, adds FXAA and uses 2x MSAA everywhere; Ultra also raises the level of "
     "detail, the mirror and particles and uses FXAA Extreme; Experimental adds 4x MSAA "
     "everywhere, scenery shadows, double reflection updates and crowds in every race. A "
     "change to one setting makes the preset Custom."},
    {"SHOWROOM MSAA", &kShowroomMsaa,
     "The game's multisampling in the showroom, menus, pre-race, replays and photo mode. 4x is "
     "the console's and draws the scene in three tiles, 2x in two, Off in one: fewer samples "
     "are faster and leave edges to the resolution scale. Applies from the next screen. Off "
     "while DLSS or FSR 2 / 3 upscales: the upscaler smooths edges itself.",
     true},
    {"RACE MSAA", &kRaceMsaa,
     "The game's multisampling in races, the countdown and the results. 2x is the console's "
     "(4x in races alone on track); 4x has twice the samples and three tiles instead of two, "
     "heavy on the GPU; Off draws one tile and leaves edges to the resolution scale. Applies "
     "from the next race. Off while DLSS or FSR 2 / 3 upscales."},
    {"ANISOTROPIC FILTERING", &kAnisotropy,
     "Keeps textures sharp at grazing angles. Game leaves each texture the filtering the game "
     "asks for."},
    {"LEVEL OF DETAIL", &kDetail,
     "How far cars keep their detailed models. Ultra also keeps the best model loaded for your "
     "car, at more guest memory per car. Applies from the next race."},
    {"SCENERY SHADOWS", &kSceneryShadows,
     "Experimental, so in no other preset: shadows of the cones, barriers and other scenery in "
     "races, which the game draws only in replays and photo mode. Known bug: a shadow vanishes "
     "or flickers as its object nears the edge of the screen, most visibly on large objects."},
    {"REFLECTION UPDATES", &kReflectionUpdates,
     "How often car reflections are redrawn. The console leaves them frozen in 16-car races "
     "and split screen; Every Race redraws them there too; Double redraws twice as many "
     "faces each frame."},
    {"MIRROR", &kMirror,
     "Full draws the mirror every frame with up to 16 cars and the cones and barriers, where "
     "the console halves its rate and draws 6 cars (12- and 16-car races)."},
    {"SOFT PARTICLES", &kSoftParticles,
     "Smoke and dust fade where they meet the ground and cars instead of cutting into them, "
     "as the game does in replays and photo mode."},
    {"CROWDS", &kCrowds,
     "Every Race adds the crowds the console leaves out of 1- and 16-car races. Uses more "
     "guest memory."},
    {"POST AA", &kPostAa,
     "Smooths edges in the finished picture. FXAA Extreme smooths more and softens more. Off "
     "while DLSS or FSR 2 / 3 upscales: the upscaler smooths edges itself.",
     true},
    {"BLOOM", &kBloom,
     "Full Rate updates the glow every frame, where the console skips every second one "
     "(16-car races)."},
    {"MOTION BLUR", &kMotionBlur,
     "Blurs the road surface with speed. Off keeps the asphalt as sharp as when standing "
     "still."},
    {"DEPTH OF FIELD", &kDepthOfField,
     "Blurs what is out of focus in the showroom, pre-race and replay cameras and photo mode. "
     "Applies from the next screen."},
    {"CAR DIRT", &kCarDirt,
     "The dirt cars gather in a race. Off leaves cars clean."},
};

const RowText& Text(const char* label) {
  for (const RowText& row : kRows) {
    if (std::string_view(row.label) == label) {
      return row;
    }
  }
  std::abort();
}

std::string JsonString(std::string_view text) {
  std::string out = "\"";
  for (const char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      default:
        out += c;
    }
  }
  return out + "\"";
}

std::string EffectiveDefault(const Setting& setting) {
  // A built-in default (built_in_defaults.cpp), over the SDK's auto.
  if (&setting == &kVsync) {
    return "immediate";
  }
  const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(setting.name);
  return info ? info->default_value : std::string();
}

std::string RowJson(const RowText& row) {
  std::string out = "{\"label\":" + JsonString(row.label);
  if (!row.setting) {
    return out + ",\"kind\":\"preset\",\"description\":" + JsonString(row.description) + "}";
  }
  const Setting& setting = *row.setting;
  out += ",\"key\":" + JsonString(setting.name);
  out += std::string(",\"string\":") + (setting.string ? "true" : "false");
  if (setting.choices.empty()) {
    out += ",\"kind\":\"range\",\"low\":" + std::to_string(setting.low) +
           ",\"high\":" + std::to_string(setting.high);
  } else {
    out += ",\"kind\":\"choice\",\"choices\":[";
    for (size_t i = 0; i < setting.choices.size(); ++i) {
      out += std::string(i ? "," : "") + "{\"label\":" + JsonString(setting.choices[i].label) +
             ",\"value\":" + JsonString(setting.choices[i].value) + "}";
    }
    out += "]";
  }
  out += ",\"default\":" + JsonString(EffectiveDefault(setting));
  if (row.shown_with) {
    out += ",\"shown_when\":{" + JsonString(row.shown_with->name) + ":" +
           JsonString(row.shown_value) + "}";
  }
  return out + ",\"description\":" + JsonString(row.description) + "}";
}

// SAVE & APPLY sets the drafted settings one per frame. After a setting that
// rebuilds the renderer or the presenter, the next waits until it is done:
// the resolution scale until the renderer draws at it (at most
// kRebuildTimeout), the upscaler and vsync for kPresenterSettle. A scale the
// driver's pipeline cache already holds switches in about 2 s; one not used
// before builds all its pipelines first (11-12 s on an RTX 4070 Ti), which
// the page says once the switch outlasts kShaderBuildShown.
constexpr auto kPresenterSettle = std::chrono::milliseconds(500);
constexpr auto kRebuildTimeout = std::chrono::seconds(60);
constexpr auto kShaderBuildShown = std::chrono::seconds(3);

std::string Live(const char* name) { return rex::cvar::GetFlagByName(name); }

int LiveNumber(const char* name) { return std::atoi(Live(name).c_str()); }

// The highest frame rate the page offers: the display's refresh rate, within
// the setting's range.
int FrameRateCeiling(uint32_t refresh_hz) {
  return refresh_hz ? std::clamp(int(refresh_hz), kFrameRate.low, kFrameRate.high)
                    : kFrameRate.high;
}

int Find(const Setting& setting, std::string_view value) {
  if (setting.choices.empty()) {
    const int number = std::atoi(std::string(value).c_str());
    return std::to_string(number) == value && number >= setting.low && number <= setting.high
               ? 0
               : -1;
  }
  for (size_t i = 0; i < setting.choices.size(); ++i) {
    if (value == setting.choices[i].value) {
      return int(i);
    }
  }
  return -1;
}

int Step(const Setting& setting, int current, int direction) {
  const int count = int(setting.choices.size());
  return current < 0 ? 0 : (current + direction + count) % count;
}

std::string Label(const Setting& setting, int index) {
  return index >= 0 ? std::string(setting.choices[size_t(index)].label) : std::string("CUSTOM");
}

bool FromCommandLine(const char* name) {
  return rex::cvar::GetFlagSource(name) == rex::cvar::Source::kCommandLine;
}

// The per-axis scales override resolution_scale when set.
bool SetByLaunchOption(const Setting& setting) {
  if (&setting == &kScale) {
    return FromCommandLine(kScale.name) || FromCommandLine("draw_resolution_scale_x") ||
           FromCommandLine("draw_resolution_scale_y");
  }
  return FromCommandLine(setting.name);
}

// The page's changes: drafted by the rows, then saved and applied together.
struct Draft {
  // The changed settings' values; a setting changed back is no longer here.
  std::map<const Setting*, std::string> values;
  // Applying: the settings still to set, in order, and the one set last while
  // the renderer or presenter takes it in.
  std::vector<const Setting*> queue;
  const Setting* settling = nullptr;
  std::chrono::steady_clock::time_point set_at;
  // Leaving was refused once over these changes; the next leave discards them.
  bool leave_warned = false;
  // Applied, with nothing changed since.
  bool applied = false;
  // The scale the UPSCALER row lowered the drafted scale to, until the next
  // change; 0 when it did not.
  uint32_t lowered_scale = 0;

  bool busy() const { return !queue.empty() || settling; }

  std::string Value(const Setting& setting) const {
    const auto it = values.find(&setting);
    return it != values.end() ? it->second : Live(setting.name);
  }

  void Change(const Setting& setting, std::string value) {
    if (value == Live(setting.name)) {
      values.erase(&setting);
    } else {
      values[&setting] = std::move(value);
    }
    leave_warned = false;
    applied = false;
    lowered_scale = 0;
  }
};

uint32_t LiveScale(const GraphicsPageServices& services) {
  uint32_t scale = services.draw_resolution_scale ? services.draw_resolution_scale() : 0;
  if (!scale) {
    scale = uint32_t(std::max(1, std::atoi(Live(kScale.name).c_str())));
  }
  return scale;
}

// Why the upscaler `value` names cannot run on this GPU, or nullopt when it can.
std::optional<std::string> UpscalerProblem(const GraphicsPageServices& services,
                                           std::string_view value) {
  if (value == "bilinear" || !services.upscalers) {
    return std::nullopt;
  }
  for (const rex::ui::UpscalerInfo& info : services.upscalers()) {
    if (info.id == value) {
      return info.available ? std::nullopt : std::optional(info.unavailable_reason);
    }
  }
  return std::string("its runtime is missing");
}

uint32_t DraftedScale(const Draft& draft, const GraphicsPageServices& services) {
  return draft.values.contains(&kScale) ? uint32_t(std::atoi(draft.Value(kScale).c_str()))
                                        : LiveScale(services);
}

// Whether the render size, at the drafted scale, fits the output. The
// presenter runs a temporal upscaler only then (temporal_fits): a larger
// render is downscaled without it.
// The largest scale whose render fits the output, 0 when none does; nullopt
// while the output size is unknown.
std::optional<uint32_t> LargestFittingScale(const GraphicsPageServices& services) {
  const auto output = services.output_size ? services.output_size() : std::nullopt;
  if (!output || !output->first || !output->second) {
    return std::nullopt;
  }
  return std::min(output->first / 1280, output->second / 720);
}

bool RenderFits(const Draft& draft, const GraphicsPageServices& services) {
  const auto largest = LargestFittingScale(services);
  return !largest || DraftedScale(draft, services) <= *largest;
}

// Whether the drafted upscaler is one this GPU runs.
bool UpscalerChosen(const Draft& draft, const GraphicsPageServices& services) {
  const std::string effect = draft.Value(kUpscaler);
  return effect != "bilinear" && !UpscalerProblem(services, effect);
}

std::string ScreenSize(const GraphicsPageServices& services) {
  const auto [width, height] = *services.output_size();
  return std::to_string(width) + "x" + std::to_string(height);
}

// The chosen upscaler, when it runs on this GPU but not at the drafted scale,
// which renders larger than the output; empty otherwise.
std::string IdleUpscaler(const Draft& draft, const GraphicsPageServices& services) {
  return !UpscalerChosen(draft, services) || RenderFits(draft, services)
             ? std::string()
             : Label(kUpscaler, Find(kUpscaler, draft.Value(kUpscaler)));
}

// Why the chosen upscaler does not run at the drafted scale, or empty.
std::string IdleUpscalerNote(const Draft& draft, const GraphicsPageServices& services) {
  const std::string upscaler = IdleUpscaler(draft, services);
  if (upscaler.empty()) {
    return upscaler;
  }
  const uint32_t largest = *LargestFittingScale(services);
  const std::string screen = ScreenSize(services);
  return (largest ? upscaler + " runs up to " + std::to_string(largest) + "x on this " + screen
                  : upscaler + " does not run on this " + screen) +
         " screen: at " + std::to_string(DraftedScale(draft, services)) +
         "x the picture is downscaled without it" +
         (upscaler == "FSR 1" ? "." : ", with MSAA and post AA instead.");
}

// The upscaler that is upscaling, or will once the drafted upscaler or scale
// is applied; empty for none.
std::string UpscalerName(const Draft& draft, const GraphicsPageServices& services) {
  if (!draft.values.contains(&kUpscaler) && !draft.values.contains(&kScale)) {
    const std::string effect = Live("present_effect");
    if (effect == "fsr") {
      return RenderFits(draft, services) ? "FSR 1" : std::string();
    }
    if (!rex::ui::IsTemporalUpscalerActive()) {
      return std::string();
    }
    return effect == "dlss" ? "DLSS" : effect == "fsr3" ? "FSR 3" : "FSR 2";
  }
  return !UpscalerChosen(draft, services) || !RenderFits(draft, services)
             ? std::string()
             : Label(kUpscaler, Find(kUpscaler, draft.Value(kUpscaler)));
}

// The upscaler that takes over the anti-aliasing; FSR 1 works on the finished
// picture and leaves it to MSAA and post AA.
std::string TemporalUpscalerName(const Draft& draft, const GraphicsPageServices& services) {
  const std::string upscaler = UpscalerName(draft, services);
  return upscaler == "FSR 1" ? std::string() : upscaler;
}

std::string ResolutionLine(const Draft& draft, const GraphicsPageServices& services) {
  const uint32_t scale = DraftedScale(draft, services);
  const uint32_t render_width = 1280 * scale;
  const uint32_t render_height = 720 * scale;
  const auto size = [](uint32_t width, uint32_t height) {
    return std::to_string(width) + "x" + std::to_string(height);
  };
  std::string line = "Renders " + size(render_width, render_height);
  const auto output = services.output_size ? services.output_size() : std::nullopt;
  if (!output || !output->first || !output->second) {
    return line;
  }
  const auto [output_width, output_height] = *output;
  const std::string upscaler = UpscalerName(draft, services);
  if (output_width == render_width && output_height == render_height) {
    // At 1:1 the upscalers only anti-alias, under the names NVIDIA and AMD give that mode;
    // FSR 1 only sharpens.
    if (upscaler.empty()) {
      return line + ", shown at that size.";
    }
    if (upscaler == "FSR 1") {
      return line + ", shown at that size, sharpened by FSR 1.";
    }
    return line + ", shown at that size with " + (upscaler == "DLSS" ? "DLAA" : upscaler + " Native AA") + ".";
  }
  if (output_width * output_height > render_width * render_height) {
    if (upscaler.empty()) {
      return line + ", stretched to " + size(output_width, output_height) + ".";
    }
    char factor[16];
    std::snprintf(factor, sizeof(factor), "%.1f", double(output_width) / double(render_width));
    return line + ", upscaled to " + size(output_width, output_height) + " by " + upscaler + " (" +
           factor + "x).";
  }
  return line + ", downscaled to " + size(output_width, output_height) + ".";
}

// FSR 1's sharpening reads its own cvar, in stops below the sharpest: SHARPNESS
// sets it as the SDK derives the temporal upscalers' (GetRcasSharpnessReduction).
void SetFsr1Sharpness() {
  constexpr const char* kReduction = "present_fsr_sharpness_reduction";
  if (FromCommandLine(kReduction)) {
    return;
  }
  const double stops = 2.0 * (1.0 - LiveNumber(kSharpness.name) / 100.0);
  rex::cvar::SetFlagByName(kReduction, std::to_string(stops));
}

void SetLive(const Setting& setting, const std::string& value) {
  if (!rex::cvar::SetFlagByName(setting.name, value)) {
    REXLOG_ERROR("Graphics page: {} refused {}", setting.name, value);
    return;
  }
  REXLOG_INFO("Graphics page: {} = {}", setting.name, value);
  if (&setting == &kSharpness) {
    SetFsr1Sharpness();
  }
  if (&setting == &kScale) {
    // The per-axis scales win over resolution_scale while either is set.
    for (const char* axis : {"draw_resolution_scale_x", "draw_resolution_scale_y"}) {
      if (rex::cvar::HasNonDefaultValue(axis)) {
        rex::cvar::SetFlagByName(axis, value);
      }
    }
  }
}

// Saves every drafted value, then queues them: the settings that rebuild
// something first, each alone.
void SaveAndApply(Draft& draft, config::HostConfig& config) {
  if (draft.busy() || draft.values.empty()) {
    return;
  }
  for (const auto& [setting, value] : draft.values) {
    config.Set(setting->name, setting->string ? config::Quote(value) : value);
  }
  if (!config.Save()) {
    REXLOG_ERROR("Graphics page: could not write {}", config.path().string());
  }
  for (const Setting* setting : {&kScale, &kUpscaler, &kVsync}) {
    if (draft.values.contains(setting)) {
      draft.queue.push_back(setting);
    }
  }
  for (const Setting* setting : kSettings) {
    if (draft.values.contains(setting) &&
        std::find(draft.queue.begin(), draft.queue.end(), setting) == draft.queue.end()) {
      draft.queue.push_back(setting);
    }
  }
  REXLOG_INFO("Graphics page: applying {} settings", draft.queue.size());
  draft.leave_warned = false;
  draft.applied = true;
}

// Whether the setting set last has taken effect, so the next may be set.
bool Settled(const Draft& draft, const GraphicsPageServices& services,
             std::chrono::steady_clock::time_point now) {
  const auto elapsed = now - draft.set_at;
  if (draft.settling != &kScale) {
    return elapsed >= kPresenterSettle;
  }
  if (!services.draw_resolution_scale ||
      std::to_string(services.draw_resolution_scale()) == Live(kScale.name)) {
    return true;
  }
  if (elapsed >= kRebuildTimeout) {
    REXLOG_WARN("Graphics page: the renderer did not take {} = {} within {} s", kScale.name,
                Live(kScale.name), kRebuildTimeout.count());
    return true;
  }
  return false;
}

// Sets the next queued setting once the one before has settled.
void ApplyNext(Draft& draft, const GraphicsPageServices& services) {
  const auto now = std::chrono::steady_clock::now();
  if (draft.settling) {
    if (!Settled(draft, services, now)) {
      return;
    }
    draft.settling = nullptr;
    if (draft.queue.empty()) {
      REXLOG_INFO("Graphics page: all settings applied");
    }
  }
  if (draft.queue.empty()) {
    return;
  }
  const Setting* setting = draft.queue.front();
  draft.queue.erase(draft.queue.begin());
  SetLive(*setting, draft.values.at(setting));
  draft.values.erase(setting);
  if (setting == &kScale || setting == &kUpscaler || setting == &kVsync) {
    draft.settling = setting;
    draft.set_at = now;
  } else if (draft.queue.empty()) {
    REXLOG_INFO("Graphics page: all settings applied");
  }
}

int DraftedPreset(const Draft& draft) {
  for (int i = 0; i < kPresetCount; ++i) {
    bool match = true;
    for (size_t j = 0; j < kPresetSettingCount && match; ++j) {
      match = draft.Value(*kPresetSettings[j]) == kPresets[i].values[j];
    }
    if (match) {
      return i;
    }
  }
  return -1;
}

MenuRow Row(const char* label, const Setting& setting, const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = label;
  row.description = Text(label).description;
  row.divider_above = Text(label).divider_above;
  row.value = [&setting, draft] { return Label(setting, Find(setting, draft->Value(setting))); };
  // A launch option sets the starting value only; the badge goes once the
  // page applies a change to the setting.
  row.badge = [&setting] {
    return SetByLaunchOption(setting) ? std::string("Set by a launch option for this session.")
                                      : std::string();
  };
  row.adjust = [&setting, draft](int direction) {
    const int next = Step(setting, Find(setting, draft->Value(setting)), direction);
    draft->Change(setting, setting.choices[size_t(next)].value);
  };
  row.unapplied = [&setting, draft] { return draft->values.contains(&setting); };
  return row;
}

// A temporal upscaler takes over the anti-aliasing (TilingChoice in
// game_hooks.cpp, the post effect in the SDK): the row says so and keeps the
// player's choice for later.
MenuRow AntiAliasingRow(const char* label, const Setting& setting,
                        const std::shared_ptr<Draft>& draft,
                        const GraphicsPageServices& services) {
  MenuRow row = Row(label, setting, draft);
  row.value = [&setting, draft, services] {
    const std::string upscaler = TemporalUpscalerName(*draft, services);
    return upscaler.empty() ? Label(setting, Find(setting, draft->Value(setting)))
                            : "OFF (" + upscaler + ")";
  };
  row.enabled = [draft, services] { return TemporalUpscalerName(*draft, services).empty(); };
  return row;
}

// Steps over the upscalers this GPU cannot run; OFF always can.
MenuRow UpscalerRow(const std::shared_ptr<Draft>& draft, const GraphicsPageServices& services) {
  MenuRow row = Row("UPSCALER", kUpscaler, draft);
  row.adjust = [draft, services](int direction) {
    int index = Find(kUpscaler, draft->Value(kUpscaler));
    do {
      index = Step(kUpscaler, index, direction);
    } while (UpscalerProblem(services, kUpscaler.choices[size_t(index)].value));
    draft->Change(kUpscaler, kUpscaler.choices[size_t(index)].value);
    // An upscaler only upscales: a scale rendering larger than the screen
    // comes down to the largest that fits, drafted like any change.
    const auto largest = LargestFittingScale(services);
    if (UpscalerChosen(*draft, services) && largest && *largest &&
        DraftedScale(*draft, services) > *largest) {
      draft->Change(kScale, std::to_string(*largest));
      draft->lowered_scale = *largest;
    }
  };
  // A chosen upscaler that cannot run at the scale says so, as the rows it
  // would take over do.
  row.value = [draft, services] {
    const std::string label = Label(kUpscaler, Find(kUpscaler, draft->Value(kUpscaler)));
    return IdleUpscaler(*draft, services).empty()
               ? label
               : label + " (OFF AT " + std::to_string(DraftedScale(*draft, services)) + "X)";
  };
  row.note = [draft, services] {
    if (draft->lowered_scale) {
      const std::string scale = std::to_string(draft->lowered_scale);
      return "Resolution scale lowered to " + scale + "x: " +
             Label(kUpscaler, Find(kUpscaler, draft->Value(kUpscaler))) + " runs up to " + scale +
             "x on this " + ScreenSize(services) + " screen.";
    }
    if (!UpscalerName(*draft, services).empty()) {
      return ResolutionLine(*draft, services);
    }
    if (std::string idle = IdleUpscalerNote(*draft, services); !idle.empty()) {
      return idle;
    }
    const int index = Find(kUpscaler, draft->Value(kUpscaler));
    const auto problem =
        index > 0 ? UpscalerProblem(services, kUpscaler.choices[size_t(index)].value)
                  : std::nullopt;
    return problem ? Label(kUpscaler, index) + " does not run here: " + *problem + "."
                   : std::string();
  };
  return row;
}

}  // namespace

bool WriteGraphicsSchema(const std::filesystem::path& path) {
  std::string out = "{\"schema_version\":1,\"blocks\":[{\"rows\":[";
  bool first = true;
  for (const RowText& row : kRows) {
    if (row.divider_above) {
      out += "]},{\"rows\":[";
      first = true;
    }
    out += (first ? "" : ",") + RowJson(row);
    first = false;
  }
  out += "]}],\"presets\":[";
  for (int i = 0; i < kPresetCount; ++i) {
    out += std::string(i ? "," : "") + "{\"label\":" + JsonString(kPresets[i].label) +
           ",\"values\":{";
    for (size_t j = 0; j < kPresetSettingCount; ++j) {
      out += std::string(j ? "," : "") + JsonString(kPresetSettings[j]->name) + ":" +
             JsonString(kPresets[i].values[j]);
    }
    out += "}}";
  }
  out += "]}\n";
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << out;
  return bool(file);
}

void ApplySavedGraphicsSettings(const config::HostConfig& config) {
  for (const Setting* setting : kSettings) {
    const auto value = config.Get(setting->name);
    if (!value) {
      continue;
    }
    if (SetByLaunchOption(*setting)) {
      REXLOG_INFO("Graphics settings: {} comes from the command line, not {}", setting->name,
                  config.path().string());
    } else if (Find(*setting, *value) < 0 || !rex::cvar::SetFlagByName(setting->name, *value)) {
      REXLOG_WARN("Graphics settings: ignored {} = {} in {}", setting->name, *value,
                  config.path().string());
    } else {
      REXLOG_INFO("Graphics settings: {} = {}", setting->name, *value);
    }
  }
  SetFsr1Sharpness();
}

uint32_t DisplayRefreshHz(void* native_window) {
#if defined(_WIN32)
  MONITORINFOEXW monitor = {};
  monitor.cbSize = sizeof(monitor);
  DEVMODEW mode = {};
  mode.dmSize = sizeof(mode);
  if (native_window &&
      GetMonitorInfoW(MonitorFromWindow(HWND(native_window), MONITOR_DEFAULTTONEAREST),
                      &monitor) &&
      EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
      mode.dmDisplayFrequency > 1) {
    return mode.dmDisplayFrequency;
  }
#endif
  return 0;
}

void LimitFrameRateToDisplay(uint32_t refresh_hz) {
  const int ceiling = FrameRateCeiling(refresh_hz);
  if (!refresh_hz || SetByLaunchOption(kFrameRate) || LiveNumber(kFrameRate.name) <= ceiling) {
    return;
  }
  rex::cvar::SetFlagByName(kFrameRate.name, std::to_string(ceiling));
  REXLOG_INFO("Graphics settings: {} lowered to {} for a {} Hz display", kFrameRate.name, ceiling,
              refresh_hz);
}

std::unique_ptr<MenuScreen> CreateGraphicsPage(config::HostConfig& config,
                                               GraphicsPageServices services) {
  auto draft = std::make_shared<Draft>();
  std::vector<MenuRow> rows;
  MenuRow frame_rate;
  frame_rate.label = "FRAME RATE";
  frame_rate.slider = hostui::MenuSlider{
      .range =
          [services] {
            return std::pair{kFrameRate.low, FrameRateCeiling(services.display_refresh_hz
                                                                  ? services.display_refresh_hz()
                                                                  : 0)};
          },
      .get = [draft] { return std::atoi(draft->Value(kFrameRate).c_str()); },
      .set = [draft](int value) { draft->Change(kFrameRate, std::to_string(value)); }};
  frame_rate.badge = [] {
    return SetByLaunchOption(kFrameRate) ? std::string("Set by a launch option for this session.")
                                         : std::string();
  };
  frame_rate.description = Text("FRAME RATE").description;
  frame_rate.note = [services] {
    const uint32_t hz = services.display_refresh_hz ? services.display_refresh_hz() : 0;
    return hz ? "Your display refreshes at " + std::to_string(hz) + " Hz." : std::string();
  };
  frame_rate.unapplied = [draft] { return draft->values.contains(&kFrameRate); };
  rows.push_back(std::move(frame_rate));

  MenuRow vsync = Row("VSYNC", kVsync, draft);
  rows.push_back(std::move(vsync));

  MenuRow scale = Row("RESOLUTION SCALE", kScale, draft);
  // While an upscaler is chosen, only the scales it can upscale from.
  scale.adjust = [draft, services](int direction) {
    const auto largest = LargestFittingScale(services);
    const bool limited = UpscalerChosen(*draft, services) && largest && *largest;
    int index = Find(kScale, draft->Value(kScale));
    do {
      index = Step(kScale, index, direction);
    } while (limited && uint32_t(std::atoi(kScale.choices[size_t(index)].value)) > *largest);
    draft->Change(kScale, kScale.choices[size_t(index)].value);
  };
  scale.note = [draft, services] {
    const std::string idle = IdleUpscalerNote(*draft, services);
    return ResolutionLine(*draft, services) + (idle.empty() ? "" : " " + idle);
  };
  rows.push_back(std::move(scale));

  rows.push_back(UpscalerRow(draft, services));

  MenuRow sharpness = Row("SHARPNESS", kSharpness, draft);
  sharpness.enabled = [draft, services] { return !UpscalerName(*draft, services).empty(); };
  rows.push_back(std::move(sharpness));

  MenuRow preset;
  preset.label = "GRAPHICS PRESET";
  preset.value = [draft] {
    const int index = DraftedPreset(*draft);
    return std::string(index >= 0 ? kPresets[index].label : "CUSTOM");
  };
  preset.adjust = [draft](int direction) {
    const int current = DraftedPreset(*draft);
    const int next = current < 0 ? (direction > 0 ? 0 : kPresetCount - 1)
                                 : (current + direction + kPresetCount) % kPresetCount;
    for (size_t j = 0; j < kPresetSettingCount; ++j) {
      draft->Change(*kPresetSettings[j], kPresets[next].values[j]);
    }
  };
  preset.description = Text("GRAPHICS PRESET").description;
  rows.push_back(std::move(preset));

  MenuRow showroom_msaa = AntiAliasingRow("SHOWROOM MSAA", kShowroomMsaa, draft, services);
  rows.push_back(std::move(showroom_msaa));

  MenuRow race_msaa = AntiAliasingRow("RACE MSAA", kRaceMsaa, draft, services);
  rows.push_back(std::move(race_msaa));

  MenuRow anisotropy = Row("ANISOTROPIC FILTERING", kAnisotropy, draft);
  rows.push_back(std::move(anisotropy));

  MenuRow detail = Row("LEVEL OF DETAIL", kDetail, draft);
  rows.push_back(std::move(detail));

  MenuRow scenery_shadows = Row("SCENERY SHADOWS", kSceneryShadows, draft);
  rows.push_back(std::move(scenery_shadows));

  MenuRow reflection_updates = Row("REFLECTION UPDATES", kReflectionUpdates, draft);
  rows.push_back(std::move(reflection_updates));

  MenuRow mirror = Row("MIRROR", kMirror, draft);
  rows.push_back(std::move(mirror));

  MenuRow soft_particles = Row("SOFT PARTICLES", kSoftParticles, draft);
  rows.push_back(std::move(soft_particles));

  MenuRow crowds = Row("CROWDS", kCrowds, draft);
  rows.push_back(std::move(crowds));

  MenuRow post_aa = AntiAliasingRow("POST AA", kPostAa, draft, services);
  rows.push_back(std::move(post_aa));

  MenuRow bloom = Row("BLOOM", kBloom, draft);
  rows.push_back(std::move(bloom));

  MenuRow motion_blur = Row("MOTION BLUR", kMotionBlur, draft);
  rows.push_back(std::move(motion_blur));

  MenuRow depth_of_field = Row("DEPTH OF FIELD", kDepthOfField, draft);
  rows.push_back(std::move(depth_of_field));

  MenuRow car_dirt = Row("CAR DIRT", kCarDirt, draft);
  rows.push_back(std::move(car_dirt));

  auto screen = std::make_unique<MenuScreen>("GRAPHICS", std::move(rows));
  screen->set_apply([draft, &config] { SaveAndApply(*draft, config); });
  // Unapplied changes keep the page open once; leaving again discards them.
  screen->set_leave([draft] {
    if (draft->busy()) {
      return false;
    }
    if (draft->values.empty() || draft->leave_warned) {
      return true;
    }
    draft->leave_warned = true;
    return false;
  });
  screen->set_status([draft] {
    if (draft->busy()) {
      if (draft->settling == &kScale &&
          std::chrono::steady_clock::now() - draft->set_at >= kShaderBuildShown) {
        return hostui::MenuStatus{
            .text = "BUILDING SHADERS FOR " + Label(kScale, Find(kScale, Live(kScale.name))) +
                    ", FIRST TIME ONLY",
            .busy = true};
      }
      return hostui::MenuStatus{.text = "APPLYING SETTINGS", .busy = true};
    }
    if (!draft->values.empty()) {
      return hostui::MenuStatus{.text = "UNSAVED CHANGES", .warning = draft->leave_warned};
    }
    if (draft->applied) {
      return hostui::MenuStatus{.text = "SETTINGS APPLIED. YOU CAN CONTINUE."};
    }
    return hostui::MenuStatus{};
  });
  screen->set_update([draft, services](bool closing) {
    if (!closing) {
      ApplyNext(*draft, services);
    } else if (!draft->values.empty() && !draft->busy()) {
      REXLOG_INFO("Graphics page: discarded {} unapplied changes", draft->values.size());
    }
  });
  return screen;
}

}  // namespace fm4_static_recomp::ui
