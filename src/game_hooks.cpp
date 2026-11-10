// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
// Hooks the recompiled game calls (config/rexglue/analysis/main-xex.toml).

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/ui/upscaler.h>

#include "audio_gain.h"
#include "custom_music.h"
#include "car_dirt.h"
#include "game_hooks.h"
#include "guest_addresses.h"
#include "input/controller_layout.h"
#include "input/input_context.h"
#include "input/prompt_strings.h"
#include "scripted_input.h"
#include "shadow_map.h"
#include "string_log.h"

REXCVAR_DEFINE_BOOL(fm4_road_detail_blur, true, "Forza Motorsport 4",
                    "Blur the road detail texture with camera motion, as the game does; off "
                    "keeps the detail the game renders for a still camera");
REXCVAR_DEFINE_BOOL(fm4_car_dirt, true, "Forza Motorsport 4",
                    "The dirt cars gather in races; off leaves cars clean");
REXCVAR_DEFINE_BOOL(fm4_depth_of_field, true, "Forza Motorsport 4",
                    "Depth of field where the game uses it (showroom, pre-race and replay "
                    "cameras, photo mode); a change applies from the next screen");
REXCVAR_DEFINE_BOOL(fm4_upscaler_replay_blur, false, "Forza Motorsport 4",
                    "Keep the replay cameras' motion blur while a temporal upscaler (DLSS, "
                    "FSR 2/3) runs; off by default: the upscaler turns its smear around the "
                    "car's mirrors and exhaust pipes into ghosting");
REXCVAR_DEFINE_INT32(fm4_frame_rate, 120, "Forza Motorsport 4",
                     "Frame rate, 60 (the console's races) to 240, or down to 30 to test slow "
                     "processors: the guest vblank rate (fh1_guest_vblank_hz) in races, "
                     "menus and the showroom")
    .range(30, 240);
REXCVAR_DEFINE_INT32(fm4_debug_vblank_hz, 0, "Forza Motorsport 4",
                     "Diagnostics: override the normal guest vblank rate, for example 360 or "
                     "480 Hz, to measure throughput above the player frame-rate range; 0 is off. "
                     "Fast loading and fm4_console_rate take precedence")
    .range(0, 2000)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(fm4_menu_vblank_hz, 0, "Forza Motorsport 4",
                     "Diagnostics: vblanks per second the game's main loop sees outside races, "
                     "the nearest whole fraction of the guest vblank (0 = every vblank)");

REXCVAR_DEFINE_BOOL(fm4_console_rate, false, "Forza Motorsport 4",
                    "Diagnostics: the console's pacing, a 60 Hz guest vblank everywhere (races "
                    "at 60 fps, the showroom and other half-rate scenes at 30); overrides "
                    "fm4_frame_rate and fm4_menu_vblank_hz");

REXCVAR_DEFINE_BOOL(fm4_full_rate_scenes, true, "Forza Motorsport 4",
                    "Render the scenes the game draws at half the frame rate (showroom, "
                    "pre-race screen, Autovista) at the full rate; false is the game's own "
                    "half rate");

REXCVAR_DEFINE_INT32(fm4_shadow_map_size, 0, "Forza Motorsport 4",
                     "Size of the car shadow map in guest pixels, 512 (the console's, 4x "
                     "MSAA) to 768 (2x MSAA), before the resolution scale; 0 is 768 while a "
                     "temporal upscaler takes a 1x frame, else 512; applies from the next "
                     "scene load");

REXCVAR_DEFINE_INT32(fm4_ground_shadow_size, 0, "Forza Motorsport 4",
                     "Size of the races' ground shadow map in guest pixels, 256 (the "
                     "console's) to 768, before the resolution scale; 0 is 512 while a "
                     "temporal upscaler takes a 1x frame, else 256; applies from the next "
                     "scene load");

REXCVAR_DEFINE_INT32(fm4_reflection_cube_size, 256, "Forza Motorsport 4",
                     "Face size of the cars' reflection cube in guest pixels, 256 (the "
                     "console's) to 512, before the resolution scale; applies from the next "
                     "scene load");

REXCVAR_DEFINE_BOOL(fm4_skip_intro, false, "Forza Motorsport 4",
                    "Skip the startup disclaimer and the studio logo videos (the game's own "
                    "disablesplashintro developer option)");

REXCVAR_DEFINE_DOUBLE(fm4_ui_speed, 1.0, "Forza Motorsport 4",
                      "Speed of the menus' animations against real time (1 = the console's)");

REXCVAR_DEFINE_BOOL(fm4_hook_census, false, "Forza Motorsport 4",
                    "Diagnostics: log each midasm hook's first hit (Hook census: <name>), for "
                    "the render tests' check that a code image reaches the hooks 1.0 does");

REXCVAR_DEFINE_INT32(fm4_car_min_substeps, 0, "Forza Motorsport 4",
                     "A car's physics substeps per tick are at least this many (0: the game's own count)");
REXCVAR_DEFINE_STRING(fm4_physics_log, "", "Forza Motorsport 4",
                      "Diagnostics: a CSV file that receives every main loop time step, every "
                      "car's physics substep count and, ten times a simulated second, the "
                      "car's state");

// The render scenario settings below force values over the game's own; each
// one's console value leaves its values to the game.
REXCVAR_DEFINE_STRING(fm4_level_of_detail, "console", "Forza Motorsport 4",
                      "Car level of detail: console, high (slightly longer LOD distances and a "
                      "looser rank floor) or ultra (longer distances, LOD0 kept and used for the "
                      "player's car, LOD0 wheels). Applies from the next race load")
    .allowed({"console", "high", "ultra"});
REXCVAR_DEFINE_BOOL(fm4_full_rate_mirror, false, "Forza Motorsport 4",
                    "Draw the mirror every frame with up to 16 cars and the cones and barriers, "
                    "where the game halves its rate and draws 6 cars (12- and 16-car races)");
REXCVAR_DEFINE_BOOL(fm4_full_rate_bloom, false, "Forza Motorsport 4",
                    "Bloom every frame, where the game skips alternate frames (16-car races)");
REXCVAR_DEFINE_BOOL(fm4_soft_particles, false, "Forza Motorsport 4",
                    "Soft particles in races, as the game does in replays and photo mode");
REXCVAR_DEFINE_BOOL(fm4_scenery_shadows, false, "Forza Motorsport 4",
                    "Shadows of cones and barriers in races, as the game does in replays and "
                    "photo mode");
REXCVAR_DEFINE_STRING(fm4_reflection_updates, "console", "Forza Motorsport 4",
                      "Faces of the cars' reflection cube redrawn per frame: console (none in "
                      "16-car races and split screen), every_race (the console's rate there too) "
                      "or double")
    .allowed({"console", "every_race", "double"});
REXCVAR_DEFINE_STRING(fm4_showroom_msaa, "4x", "Forza Motorsport 4",
                      "MSAA of the showroom, menus, pre-race, replays and photo mode: 4x (the "
                      "game's, three tiles), 2x (two) or off (one)")
    .allowed({"4x", "2x", "off"});
REXCVAR_DEFINE_STRING(fm4_race_msaa, "2x", "Forza Motorsport 4",
                      "MSAA of races, the countdown and the results: 2x (the game's, two "
                      "tiles; 4x in races alone on track), 4x (three) or off (one)")
    .allowed({"4x", "2x", "off"});
REXCVAR_DEFINE_BOOL(fm4_crowds_every_race, false, "Forza Motorsport 4",
                    "Crowds in every race, where the game leaves them out (1- and 16-car races)");
REXCVAR_DEFINE_STRING(fm4_render_scenario_omit, "", "Forza Motorsport 4",
                      "Diagnostics: render scenario values left to the game, comma-separated: "
                      "tiling, lod_thresholds, focus_lod, other_lod, lod_distances, keep_lod0, "
                      "force_no_lod0, wheel_lod, mirror_collidables, collidable_shadows, crowds, "
                      "reflections, mirror_cars, mirror_half_rate, bloom_half_rate, "
                      "soft_particles");
REXCVAR_DEFINE_STRING(fm4_graphics_lod_thresholds, "", "Forza Motorsport 4",
                      "Diagnostics: the car LOD rank thresholds of fm4_level_of_detail high and "
                      "ultra as lod1,lod2,count (the console's are 2,4,6: the two nearest other "
                      "cars may use the best LOD, the next two the middle one, and both limits "
                      "drop by one once six cars are visible)");

namespace {

// One log line per hook function on its first hit, under fm4_hook_census; after it, a
// relaxed load (hot hooks run per string lookup or per car substep).
#define FM4_HOOK_CENSUS()                                        \
  do {                                                           \
    static std::atomic_flag fm4_hook_hit;                        \
    if (!fm4_hook_hit.test(std::memory_order_relaxed) &&         \
        !fm4_hook_hit.test_and_set(std::memory_order_relaxed)) { \
      NoteHookHit(__func__);                                     \
    }                                                            \
  } while (false)

void NoteHookHit(const char* hook) {
  if (REXCVAR_GET(fm4_hook_census)) {
    REXLOG_INFO("Hook census: {}", hook);
  }
}

std::atomic<bool> g_race_session{false};
// Set when the switches below own gpu_pixel_shader_constant_override (none was given at launch).
std::atomic<bool> g_switches_own_override{false};
std::atomic<bool> g_applied_replay_blur{true};
// The main loop's measured milliseconds since the showroom's and the UI's last update.
std::atomic<uint32_t> g_showroom_pending_ms{0};
std::atomic<uint32_t> g_ui_pending_ms{0};

// The guest vblank paces the main loop, which waits for one each tick, and D3D
// flips each swap on one. Races and menus wake on every vblank, so the frame rate
// is the vblank rate; a fixed menu rate above the display's refresh fills a FIFO
// swapchain's queue (about 100 ms of lag at 60 Hz).
constexpr char kVblankHzOption[] = "fh1_guest_vblank_hz";
constexpr double kConsoleVblankHz = 60.0;
// A race load advances partly once per main loop tick, so the load runs on a
// fast vblank from the loading screen until the cars step (5.8 s at 120 Hz,
// 3.5 s at 480 Hz).
constexpr double kLoadVblankHz = 480.0;
std::atomic<bool> g_fast_load{false};
// A quit to the menus opens the loading screen while the race's cars still
// step, so only a car step after a tick without any ends a load.
std::atomic<bool> g_car_stepped{false};
std::atomic<bool> g_fast_load_cars_stopped{false};
std::mutex g_fast_load_mutex;
std::chrono::steady_clock::time_point g_fast_load_start;

void EndFastLoad(const char* reason, bool after_start_only = false) {
  if (!g_fast_load.load(std::memory_order_relaxed)) {
    return;
  }
  const std::lock_guard lock(g_fast_load_mutex);
  const auto elapsed = std::chrono::steady_clock::now() - g_fast_load_start;
  // The menu's last ticks can still run just after the loading screen opens.
  if (!g_fast_load.load(std::memory_order_relaxed) ||
      (after_start_only && elapsed < std::chrono::seconds(1))) {
    return;
  }
  g_fast_load.store(false, std::memory_order_relaxed);
  REXLOG_INFO("Race load: fast vblank off after {:.2f} s ({})",
              std::chrono::duration<double>(elapsed).count(), reason);
}

// The PC OPTIONS entry of My Profile -> Options (the GRAPHICS and KEYBOARD
// tabs): an action and a string key of its own. The key is free in every string table (IDS_Graphics' hash, 0x7157,
// is LiveStrings' IDS_MatchmakingBlockedTitle).
constexpr uint32_t kGraphicsAction = 23;
constexpr uint32_t kGraphicsStringKey = 0x7158;
using fm4_guest::kMakeLocString;
using fm4_guest::kMakeMenuLabel;
using fm4_guest::kMenuAddItem;

std::mutex g_graphics_page_mutex;
std::function<void()> g_graphics_page_opener;
std::mutex g_input_tick_mutex;
std::function<void()> g_input_tick;

// The prompt text source and the guest copies of the texts it made, kept for
// as long as the game may hold them.
std::mutex g_prompt_text_mutex;
std::function<std::optional<std::u16string>(uint16_t, std::u16string_view)> g_prompt_text_source;
std::unordered_map<std::u16string, uint32_t> g_prompt_texts;

std::mutex g_string_log_mutex;
fm4_static_recomp::StringShownLog g_string_log;

using fm4_guest::kStringLookup;
using fm4_guest::kTextAssign;
using fm4_guest::kTextSetupGetTextReturn;

// Scene text elements built from a prompt string (element data -> its key, the table's text,
// the text it shows), so a device switch can rewrite them while their scene is up.
struct LivePromptText {
  uint16_t key;
  std::u16string original;
  std::u16string shown;
  // The layout after a rewrite measures the text it replaced (ALIGN_TO lines hug that width),
  // so the element lays out once more on its next update.
  bool relayout = false;
};
std::mutex g_live_prompt_mutex;
std::unordered_map<uint32_t, LivePromptText> g_live_prompts;

// A text element's text is a wide string at +0x84 (a 16-byte inline buffer while the capacity
// is below 8, length at +0x94, capacity at +0x98); the element lays it out again while +0x119
// is set, as the game's own SetText does.
constexpr uint32_t kTextElementString = 0x84;
constexpr uint32_t kTextElementLength = 0x94;
constexpr uint32_t kTextElementCapacity = 0x98;
constexpr uint32_t kTextElementLayout = 0x119;

// A NUL-terminated big-endian UTF-16 copy of the text on the system heap.
uint32_t GuestText(std::u16string_view text) {
  const uint32_t address =
      rex::system::kernel_state()->memory()->SystemHeapAlloc(uint32_t(2 * (text.size() + 1)));
  uint8_t* const host = rex::system::kernel_state()->memory()->virtual_membase() + address;
  for (size_t i = 0; i <= text.size(); ++i) {
    const char16_t c = i < text.size() ? text[i] : u' ';
    host[2 * i] = uint8_t(c >> 8);
    host[2 * i + 1] = uint8_t(c);
  }
  return address;
}

std::u16string ReadGuestText(const uint8_t* base, uint32_t address) {
  std::u16string text;
  for (const uint8_t* at = base + address; text.size() < 4096; at += 2) {
    const char16_t c = char16_t(at[0] << 8 | at[1]);
    if (!c) {
      break;
    }
    text.push_back(c);
  }
  return text;
}

uint32_t LoadGuestWord(const uint8_t* base, uint32_t address);

std::u16string ReadElementText(const uint8_t* base, uint32_t element) {
  const uint32_t length = LoadGuestWord(base, element + kTextElementLength);
  const uint32_t capacity = LoadGuestWord(base, element + kTextElementCapacity);
  if (length > capacity) {
    return {};
  }
  const uint32_t data = capacity < 8 ? element + kTextElementString
                                     : LoadGuestWord(base, element + kTextElementString);
  std::u16string text;
  for (uint32_t i = 0; i < length; ++i) {
    const uint8_t* const at = base + data + 2 * i;
    text.push_back(char16_t(at[0] << 8 | at[1]));
  }
  return text;
}

// Calls a guest function from a hook on a copy of the live context, below the
// hooked frame's stack, leaving the hooked code's registers as they were.
uint32_t CallGuest(PPCContext& ctx, uint8_t* base, uint32_t stack, uint32_t address,
                   std::initializer_list<uint32_t> args) {
  PPCFunc* const function = rex::runtime::ResolveIndirectFunction(address);
  if (!function) {
    return 0;
  }
  const uint32_t saved_csr = ctx.fpscr.csr;
  PPCContext nested = ctx;
  nested.dispatch_address = 0;
  nested.lr = 0;
  nested.r1.u32 = stack - 0x70;
  PPCRegister* const registers[] = {&nested.r3, &nested.r4, &nested.r5,
                                    &nested.r6, &nested.r7, &nested.r8};
  size_t index = 0;
  for (uint32_t arg : args) {
    registers[index++]->u64 = arg;
  }
  function(nested, base);
  ctx.fpscr.csr = saved_csr;
  ctx.fpscr.setcsr(saved_csr);
  return nested.r3.u32;
}

// The settings fill the override layer of the game's render scenario manager, which
// its accessors consult before the stack of RenderScenarios\*.xml files, so they win in
// every scenario. Ids are CDynamicRenderSettings control ids.
struct ScenarioValue {
  uint32_t id;
  bool byte;
  uint32_t count;
  std::array<uint32_t, 3> words;
};

constexpr ScenarioValue Byte(uint32_t id, uint8_t value) { return {id, true, 1, {value}}; }

constexpr ScenarioValue Words(uint32_t id, uint32_t a, uint32_t b = 0, uint32_t c = 0,
                              uint32_t count = 1) {
  return {id, false, count, {a, b, c}};
}

constexpr ScenarioValue Floats(uint32_t id, float a, float b, float c) {
  return {id, false, 3,
          {std::bit_cast<uint32_t>(a), std::bit_cast<uint32_t>(b), std::bit_cast<uint32_t>(c)}};
}

constexpr ScenarioValue Float(uint32_t id, float value) {
  return {id, false, 1, {std::bit_cast<uint32_t>(value)}};
}

enum : uint32_t {
  kTilingScenario = 1,
  kUITilingScenario = 2,
  kCarLODThresholds = 7,
  kCarFocusLODMinMax = 8,
  kCarNonFocusLODMinMax = 9,
  kCarLODTransDist = 10,
  kCarKeepLOD0 = 11,
  kCarForceNoLOD0 = 12,
  kCarMinWheelLOD = 13,
  kCollidablesInMirror = 18,
  kCollidableShadows = 19,
  kCrowdLoad = 20,
  kEnvMapFrequencyScale = 29,
  kMaxCarsInMirror = 30,
  kHalfRateMirror = 31,
  kHalfRateBloom = 32,
  kSoftParticles = 33,
};

constexpr ScenarioValue kHighDetail[] = {
    Words(kCarLODThresholds, 3, 5, 7, 3),
    Floats(kCarLODTransDist, 20.0f, 35.0f, 70.0f),
};

// Other cars keep the game's LOD range (CarNonFocusLODMinMax 0..5), and the
// distances let them reach LOD3 as on the console: a car held at LOD2 or
// better far away keeps a projected shadow the game does not expect there,
// which then shows only its outline for a moment.
constexpr ScenarioValue kUltraDetail[] = {
    Words(kCarLODThresholds, 4, 6, 8, 3),
    Floats(kCarLODTransDist, 30.0f, 60.0f, 120.0f),
    Words(kCarMinWheelLOD, 0),
    Byte(kCarKeepLOD0, 1),
    Byte(kCarForceNoLOD0, 0),
    Words(kCarFocusLODMinMax, 0, 0, 0, 2),
};

constexpr ScenarioValue kFullRateMirror[] = {
    Byte(kHalfRateMirror, 0),
    Words(kMaxCarsInMirror, 16),
    Byte(kCollidablesInMirror, 1),
};
constexpr ScenarioValue kFullRateBloom[] = {Byte(kHalfRateBloom, 0)};
constexpr ScenarioValue kSoftParticlesOn[] = {Byte(kSoftParticles, 1)};
constexpr ScenarioValue kSceneryShadowsOn[] = {Byte(kCollidableShadows, 1)};
constexpr ScenarioValue kReflectionsEveryRace[] = {Float(kEnvMapFrequencyScale, 1.0f)};
constexpr ScenarioValue kReflectionsDouble[] = {Float(kEnvMapFrequencyScale, 2.0f)};
constexpr ScenarioValue kCrowdsEveryRace[] = {Byte(kCrowdLoad, 1)};

constexpr std::pair<std::string_view, uint32_t> kScenarioValueNames[] = {
    {"tiling", kTilingScenario},
    {"lod_thresholds", kCarLODThresholds},
    {"focus_lod", kCarFocusLODMinMax},
    {"other_lod", kCarNonFocusLODMinMax},
    {"lod_distances", kCarLODTransDist},
    {"keep_lod0", kCarKeepLOD0},
    {"force_no_lod0", kCarForceNoLOD0},
    {"wheel_lod", kCarMinWheelLOD},
    {"mirror_collidables", kCollidablesInMirror},
    {"collidable_shadows", kCollidableShadows},
    {"crowds", kCrowdLoad},
    {"reflections", kEnvMapFrequencyScale},
    {"mirror_cars", kMaxCarsInMirror},
    {"mirror_half_rate", kHalfRateMirror},
    {"bloom_half_rate", kHalfRateBloom},
    {"soft_particles", kSoftParticles},
};

// The ids fm4_render_scenario_omit names; read once, as the option is a
// launch option.
// fm4_graphics_lod_thresholds as three numbers, or nothing.
std::optional<std::array<uint32_t, 3>> LodThresholdsOverride() {
  static const std::optional<std::array<uint32_t, 3>> thresholds =
      []() -> std::optional<std::array<uint32_t, 3>> {
    std::array<uint32_t, 3> values;
    const std::string text = REXCVAR_GET(fm4_graphics_lod_thresholds);
    if (std::sscanf(text.c_str(), "%u,%u,%u", &values[0], &values[1], &values[2]) != 3) {
      return std::nullopt;
    }
    return values;
  }();
  return thresholds;
}

uint64_t OmittedScenarioMask() {
  static const uint64_t mask = [] {
    uint64_t omitted = 0;
    const std::string list = REXCVAR_GET(fm4_render_scenario_omit);
    size_t at = 0;
    while (at <= list.size()) {
      size_t end = list.find(',', at);
      if (end == std::string::npos) {
        end = list.size();
      }
      const std::string_view name = std::string_view(list).substr(at, end - at);
      for (const auto& [known, id] : kScenarioValueNames) {
        if (name == known) {
          omitted |= uint64_t(1) << id;
        }
      }
      at = end + 1;
    }
    return omitted;
  }();
  return mask;
}

constexpr uint64_t ValueMask(std::span<const ScenarioValue> values) {
  uint64_t mask = 0;
  for (const ScenarioValue& value : values) {
    mask |= uint64_t(1) << value.id;
  }
  return mask;
}

constexpr uint64_t kSettingsOwnedMask =
    ValueMask(kHighDetail) | ValueMask(kUltraDetail) | ValueMask(kFullRateMirror) |
    ValueMask(kFullRateBloom) | ValueMask(kSoftParticlesOn) | ValueMask(kSceneryShadowsOn) |
    ValueMask(kReflectionsEveryRace) | ValueMask(kCrowdsEveryRace);

using fm4_guest::kControlOffsetTable;
using fm4_guest::kScenarioManagerPointer;
constexpr uint32_t kOverrideValueOffset = 0x40;
constexpr uint32_t kOverrideMaskOffset = 0x60;

uint32_t LoadGuestWord(const uint8_t* address) {
  uint32_t word;
  std::memcpy(&word, address, sizeof(word));
  return std::byteswap(word);
}

void StoreGuestWord(uint8_t* address, uint32_t value) {
  value = std::byteswap(value);
  std::memcpy(address, &value, sizeof(value));
}

// Above 768 the variance map's colour and depth surfaces fit the 10 MB of
// EDRAM only without MSAA (1024 at 1x); sizes that lose the MSAA are not offered.
uint32_t ShadowMapSize() {
  const auto* graphics = rex::system::kernel_state()->emulator()->graphics_system();
  return fm4_static_recomp::ShadowMapSize(REXCVAR_GET(fm4_shadow_map_size),
                                          graphics ? graphics->draw_resolution_scale() : 1,
                                          rex::ui::IsTemporalUpscalerActive());
}

// The cube's faces render with 4x MSAA: above 512 their colour and depth no
// longer fit the 10 MB of EDRAM.
uint32_t ReflectionCubeSize() {
  return uint32_t(std::clamp(REXCVAR_GET(fm4_reflection_cube_size), 256, 512));
}

// Guest physical memory for a texture the game would place in its shared
// memory, which has room for the console size only. One buffer for the
// session; a setup that needs more replaces it, and every setup rewrites the
// texture header, so nothing keeps the old address.
class SessionBuffer {
 public:
  uint32_t Get(uint32_t bytes, const char* name) {
    if (size_ >= bytes) {
      return address_;
    }
    auto* const memory = rex::system::kernel_state()->memory();
    if (address_) {
      memory->SystemHeapFree(address_);
    }
    address_ = memory->SystemHeapAlloc(bytes, 0x1000, rex::memory::kSystemHeapPhysical);
    size_ = address_ ? bytes : 0;
    REXLOG_INFO("{}: {} bytes at {:08X}", name, bytes, address_);
    return address_;
  }

 private:
  uint32_t address_ = 0;
  uint32_t size_ = 0;
};

// The override layer holds one value for every scenario, but the pre-race and
// race phases need different MSAA, so SHOWROOM MSAA and RACE MSAA rewrite the
// tiling in the scenario files themselves, each file as it is pushed (the
// Fm4ScenarioPush hook says why). A setting at the game's own choice writes the
// file's value back: 1-car races are 4x on the console, unlike the rest. The
// splitscreen files keep their 1280x360 tiling.
enum class TilingGroup { kShowroom, kRace };

struct ScenarioFile {
  uint32_t address;
  TilingGroup group;
  // Indexed by control id; only the ids the file sets.
  std::array<std::optional<uint32_t>, 3> tiling;
};

std::mutex g_scenario_files_mutex;
std::vector<ScenarioFile> g_scenario_files;

constexpr uint32_t kScenarioFileMaskOffset = 0x24;
constexpr uint32_t kTilingIds[] = {kTilingScenario, kUITilingScenario};

std::optional<TilingGroup> GroupOf(std::string_view name) {
  if (name.starts_with("Splitscreen")) {
    return std::nullopt;
  }
  if (name.ends_with("_Race") || name.ends_with("_321_and_PostRace")) {
    return TilingGroup::kRace;
  }
  if (name.ends_with("_PreRace") || name.ends_with("_Replay") || name == "PhotoMode" ||
      name.starts_with("UI_")) {
    return TilingGroup::kShowroom;
  }
  return std::nullopt;
}

uint8_t* ScenarioControlValue(uint8_t* membase, uint32_t file, uint32_t id) {
  return membase + file + LoadGuestWord(membase + kControlOffsetTable + 4 * id) + 4;
}

// Set while the game renders a car's garage thumbnail.
std::atomic<bool> g_thumbnail_render{false};

// The TilingScenario ids of the game's table: 4x MSAA in three tiles, 2x in two,
// none in one. A temporal upscaler does the anti-aliasing itself, so it gets
// one tile whatever the rows say; the rows keep the player's choice for later.
// A thumbnail keeps the game's own tiling: in one tile it loses the car's
// ground shadow, and the saved thumbnail keeps that for good.
std::optional<uint32_t> TilingChoice(TilingGroup group) {
  if (g_thumbnail_render.load(std::memory_order_relaxed)) {
    return std::nullopt;
  }
  if (rex::ui::IsTemporalUpscalerActive()) {
    return 2u;
  }
  const std::string& choice =
      group == TilingGroup::kRace ? REXCVAR_GET(fm4_race_msaa) : REXCVAR_GET(fm4_showroom_msaa);
  if (choice == (group == TilingGroup::kRace ? "2x" : "4x")) {
    return std::nullopt;
  }
  return choice == "4x" ? 0u : choice == "2x" ? 1u : 2u;
}

void ApplyScenarioFileTiling(uint8_t* membase, const ScenarioFile& file) {
  if (OmittedScenarioMask() & (uint64_t(1) << kTilingScenario)) {
    return;
  }
  const std::optional<uint32_t> choice = TilingChoice(file.group);
  for (const uint32_t id : kTilingIds) {
    if (file.tiling[id]) {
      StoreGuestWord(ScenarioControlValue(membase, file.address, id),
                     choice.value_or(*file.tiling[id]));
    }
  }
}

void ApplyRenderScenarioSettings() {
  std::array<std::span<const ScenarioValue>, 8> groups;
  size_t count = 0;
  const std::string& detail = REXCVAR_GET(fm4_level_of_detail);
  if (detail == "high") {
    groups[count++] = kHighDetail;
  } else if (detail == "ultra") {
    groups[count++] = kUltraDetail;
  }
  if (REXCVAR_GET(fm4_full_rate_mirror)) {
    groups[count++] = kFullRateMirror;
  }
  if (REXCVAR_GET(fm4_full_rate_bloom)) {
    groups[count++] = kFullRateBloom;
  }
  if (REXCVAR_GET(fm4_soft_particles)) {
    groups[count++] = kSoftParticlesOn;
  }
  if (REXCVAR_GET(fm4_scenery_shadows)) {
    groups[count++] = kSceneryShadowsOn;
  }
  const std::string& reflections = REXCVAR_GET(fm4_reflection_updates);
  if (reflections == "every_race") {
    groups[count++] = kReflectionsEveryRace;
  } else if (reflections == "double") {
    groups[count++] = kReflectionsDouble;
  }
  if (REXCVAR_GET(fm4_crowds_every_race)) {
    groups[count++] = kCrowdsEveryRace;
  }
  uint8_t* const membase = rex::system::kernel_state()->memory()->virtual_membase();
  const uint32_t manager_address = LoadGuestWord(membase + kScenarioManagerPointer);
  if (!manager_address) {
    return;
  }
  uint8_t* const manager = membase + manager_address;
  const uint64_t omitted = OmittedScenarioMask();
  uint64_t mask = 0;
  for (size_t group = 0; group < count; ++group) {
    for (const ScenarioValue& value : groups[group]) {
      if (omitted & (uint64_t(1) << value.id)) {
        continue;
      }
      mask |= uint64_t(1) << value.id;
      uint8_t* const target = manager + kOverrideValueOffset +
                              LoadGuestWord(membase + kControlOffsetTable + 4 * value.id);
      std::array<uint32_t, 3> words = value.words;
      if (value.id == kCarLODThresholds && LodThresholdsOverride()) {
        words = *LodThresholdsOverride();
      }
      if (value.byte) {
        *target = uint8_t(words[0]);
      } else {
        for (uint32_t i = 0; i < value.count; ++i) {
          StoreGuestWord(target + 4 * i, words[i]);
        }
      }
    }
  }
  for (uint32_t half = 0; half < 2; ++half) {
    uint8_t* const word = manager + kOverrideMaskOffset + 4 * half;
    const uint32_t owned = uint32_t(kSettingsOwnedMask >> (32 * half));
    StoreGuestWord(word, (LoadGuestWord(word) & ~owned) | uint32_t(mask >> (32 * half)));
  }
}

uint32_t LoadGuestWord(const uint8_t* base, uint32_t address) {
  return LoadGuestWord(base + address);
}

std::mutex g_physics_log_mutex;

std::FILE* PhysicsLog() {
  static std::FILE* const file = [] {
    const std::string path = REXCVAR_GET(fm4_physics_log);
    return path.empty() ? nullptr : std::fopen(path.c_str(), "w");
  }();
  return file;
}

void LogCarStep(uint32_t car, uint32_t substeps, uint32_t low_detail, double seconds) {
  g_car_stepped.store(true, std::memory_order_relaxed);
  if (g_fast_load_cars_stopped.load(std::memory_order_relaxed)) {
    EndFastLoad("first car step");
  }
  std::FILE* const log = PhysicsLog();
  if (!log) {
    return;
  }
  struct Clock {
    double time = 0, next_state = 0;
  };
  static std::unordered_map<uint32_t, Clock> clocks;
  const std::lock_guard lock(g_physics_log_mutex);
  Clock& clock = clocks[car];
  clock.time += seconds;
  std::fprintf(log, "c,%08X,%u,%u,%.6f,%.4f\n", car, low_detail, std::max(1u, substeps), seconds,
               clock.time);
  if (clock.time < clock.next_state) {
    return;
  }
  clock.next_state = clock.time + 0.1;
  const uint8_t* const state =
      rex::system::kernel_state()->memory()->virtual_membase() + car;
  std::fprintf(log, "s,%08X,%.4f", car, clock.time);
  for (uint32_t offset = 0; offset < 0x80; offset += 4) {
    uint32_t word;
    std::memcpy(&word, state + offset, sizeof(word));
    std::fprintf(log, ",%.5g", std::bit_cast<float>(std::byteswap(word)));
  }
  std::fputc('\n', log);
}

uint32_t CarMinSubsteps() {
  return static_cast<uint32_t>(std::max(0, REXCVAR_GET(fm4_car_min_substeps)));
}

bool ReplayBlur() {
  return REXCVAR_GET(fm4_upscaler_replay_blur) || !rex::ui::IsTemporalUpscalerActive();
}

void ApplyPixelShaderConstantOverride() {
  const bool replay_blur = ReplayBlur();
  g_applied_replay_blur.store(replay_blur, std::memory_order_relaxed);
  rex::cvar::SetFlagByName("gpu_pixel_shader_constant_override",
                           fm4_static_recomp::ComposePixelShaderConstantOverride(
                               REXCVAR_GET(fm4_road_detail_blur), REXCVAR_GET(fm4_car_dirt),
                               replay_blur));
}
}  // namespace

void Fm4MainLoopStep(PPCRegister& r30, PPCRegister& f31) {
  FM4_HOOK_CENSUS();
  g_showroom_pending_ms.fetch_add(r30.u32, std::memory_order_relaxed);
  g_ui_pending_ms.fetch_add(r30.u32, std::memory_order_relaxed);
  if (!g_car_stepped.exchange(false, std::memory_order_relaxed) &&
      g_fast_load.load(std::memory_order_relaxed)) {
    g_fast_load_cars_stopped.store(true, std::memory_order_relaxed);
  }
  if (std::FILE* const log = PhysicsLog()) {
    const std::lock_guard lock(g_physics_log_mutex);
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    std::fprintf(log, "f,%u,%.6f,%lld\n", r30.u32, f31.f64,
                 static_cast<long long>(
                     std::chrono::duration_cast<std::chrono::microseconds>(now).count()));
    std::fflush(log);
  }
}

void Fm4PhysicsLogCar(PPCRegister& r30, PPCRegister& r31, PPCRegister& r28, PPCRegister& f31) {
  FM4_HOOK_CENSUS();
  // A floor on the count, not a higher CarPhysicsUpdateHz: it follows the step a tick really took,
  // whatever FRAME RATE asks for. Set r28 means the hard-coded 239 Hz, which the mod's ini never reaches.
  if ((r28.u32 & 0xFF) == 0) {
    r31.u32 = std::max(r31.u32, CarMinSubsteps());
  }
  LogCarStep(r30.u32, r31.u32, r28.u32 & 0xFF, f31.f64);
}

void Fm4PhysicsLogOtherCar(PPCRegister& r30, PPCRegister& r11, PPCRegister& f31) {
  FM4_HOOK_CENSUS();
  r11.u32 = std::max(r11.u32, CarMinSubsteps());
  LogCarStep(r30.u32, r11.u32, 2, f31.f64);
}

void Fm4LoadingScreen() {
  FM4_HOOK_CENSUS();
  const std::lock_guard lock(g_fast_load_mutex);
  if (g_fast_load.load(std::memory_order_relaxed)) {
    return;
  }
  g_fast_load_start = std::chrono::steady_clock::now();
  g_fast_load_cars_stopped.store(false, std::memory_order_relaxed);
  g_fast_load.store(true, std::memory_order_relaxed);
  REXLOG_INFO("Race load: fast vblank on");
}

void Fm4UiSceneStep(PPCRegister& f31) {
  FM4_HOOK_CENSUS();
  // A load that ends in a menu instead of a race.
  EndFastLoad("menu", true);
  const uint32_t ms = g_ui_pending_ms.exchange(0, std::memory_order_relaxed);
  f31.f64 = std::min(ms, 100u) / 1000.0 * REXCVAR_GET(fm4_ui_speed);
}

void Fm4ShowroomCameraStep(PPCRegister& f1) {
  FM4_HOOK_CENSUS();
  EndFastLoad("showroom", true);
  // At most the 1/60 s it replaces times six, so the first update after the
  // showroom opens does not jump the camera by the time spent elsewhere.
  const uint32_t ms = g_showroom_pending_ms.exchange(0, std::memory_order_relaxed);
  f1.f64 = std::min(ms, 100u) / 1000.0;
}

void Fm4AutovistaCameraUpdate() {
  FM4_HOOK_CENSUS();
  // Runs every tick while Autovista is open, and nowhere else.
  static auto last = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  if (now - last > std::chrono::seconds(1)) {
    fm4_static_recomp::MarkInputEvent("autovista");
  }
  last = now;
}

void Fm4SplashIntroOption(PPCRegister& r23) {
  FM4_HOOK_CENSUS();
  constexpr uint32_t kDisableSplashIntro = 0xB5C;
  if (REXCVAR_GET(fm4_skip_intro)) {
    rex::system::kernel_state()->memory()->virtual_membase()[r23.u32 + kDisableSplashIntro] = 1;
  }
}

void Fm4DepthOfFieldOption(PPCRegister& r4) {
  FM4_HOOK_CENSUS();
  if (!REXCVAR_GET(fm4_depth_of_field)) {
    r4.u64 = 0;
  }
}

void Fm4PresentInterval(PPCRegister& r4) {
  FM4_HOOK_CENSUS();
  // Set every frame in races and on the pre-race screen.
  static uint32_t last = 0;
  if (r4.u32 != last) {
    last = r4.u32;
    REXLOG_INFO("Present interval: {}", last);
  }
  if (r4.u32 > 1 && REXCVAR_GET(fm4_full_rate_scenes)) {
    r4.u64 = 1;
  }
}

void Fm4ShadowMapSetup(PPCRegister& r4, PPCRegister& r5) {
  FM4_HOOK_CENSUS();
  const uint32_t size = ShadowMapSize();
  const auto* graphics = rex::system::kernel_state()->emulator()->graphics_system();
  const uint32_t ground = fm4_static_recomp::GroundShadowSize(
      REXCVAR_GET(fm4_ground_shadow_size), graphics ? graphics->draw_resolution_scale() : 1,
      rex::ui::IsTemporalUpscalerActive());
  REXLOG_INFO("Shadow map setup: variance {} grey {}, settings {} / {}", r4.u32, r5.u32, size,
              ground);
  // Only the races' 256 map: the showroom-type scenes already draw it at 512.
  if (r5.u32 == 256) {
    r5.u64 = ground;
  }
  if (r4.u32 == 512) {
    r4.u64 = size;
  }
}

bool Fm4ShadowMapKeepHeader(PPCRegister& r30, PPCRegister& r8) {
  FM4_HOOK_CENSUS();
  if (r30.u32 <= 512) {
    return false;
  }
  r8.u64 = 0;
  return true;
}

void Fm4ShadowMapMemory(PPCRegister& r4, PPCRegister& r26) {
  FM4_HOOK_CENSUS();
  constexpr uint32_t kConsoleBytes = 512 * 512 * 4;
  if (r26.u32 <= kConsoleBytes) {
    return;
  }
  static SessionBuffer buffer;
  if (const uint32_t address = buffer.Get(r26.u32, "Shadow map")) {
    r4.u64 = address;
    r26.u64 = 0;
  }
}

void Fm4ShadowPassSetup(PPCRegister& r3, PPCRegister& r4, PPCRegister& r6) {
  FM4_HOOK_CENSUS();
  REXLOG_INFO("Shadow pass setup: {} (memory {:08X}, {:08X})", r3.u32, r4.u32, r6.u32);
  if (r3.u32 != 512 || ShadowMapSize() == 512) {
    return;
  }
  r3.u64 = ShadowMapSize();
  // The pass also places a size x size 32-bit texture at r4, with only the
  // console size free there before r6.
  static SessionBuffer buffer;
  if (r4.u32) {
    if (const uint32_t address = buffer.Get(r3.u32 * r3.u32 * 4, "Shadow pass texture")) {
      r4.u64 = address;
    }
  }
}

void Fm4ShadowPassMsaa(PPCRegister& r30, PPCRegister& r31) {
  FM4_HOOK_CENSUS();
  constexpr uint64_t kEdramBytes = 0xA00000;
  if (r30.u32 != 0) {
    return;
  }
  const uint8_t* const entry =
      rex::system::kernel_state()->memory()->virtual_membase() + r31.u32;
  const uint64_t width = LoadGuestWord(entry + 0xD0);
  const uint64_t height = LoadGuestWord(entry + 0xD4);
  if (LoadGuestWord(entry + 0xD8) == 4 && width * height * 2 * 8 <= kEdramBytes) {
    r30.u64 = 1;
  }
}

void Fm4ReflectionCubeSetup(PPCRegister& r4) {
  FM4_HOOK_CENSUS();
  const uint32_t size = ReflectionCubeSize();
  REXLOG_INFO("Reflection cube setup: {}, setting {}", r4.u32, size);
  if (r4.u32 == 256 && size != 256) {
    r4.u64 = size;
  }
}

void Fm4ReflectionCubeMemory(PPCRegister& r3, PPCRegister& r26, PPCRegister& r30,
                             PPCRegister& r31) {
  FM4_HOOK_CENSUS();
  if (r26.u32 <= 256) {
    return;
  }
  // A cube above the shared memory's 1.5 MB asks the game's heap for its own
  // (kept at +0x13C0); when that fails, as it does in races, the game falls
  // back to the shared memory and overruns what follows it.
  const uint8_t* const renderer =
      rex::system::kernel_state()->memory()->virtual_membase() + r31.u32;
  if (LoadGuestWord(renderer + 0x13C0) == r3.u32) {
    return;
  }
  REXLOG_INFO("Reflection cube: {} bytes, the game's heap had no room", r30.u32);
  static SessionBuffer buffer;
  if (const uint32_t address = buffer.Get(r30.u32, "Reflection cube")) {
    r3.u64 = address;
  }
}

void Fm4ScenarioFileLoaded(PPCRegister& r1, PPCRegister& r31) {
  FM4_HOOK_CENSUS();
  uint8_t* const membase = rex::system::kernel_state()->memory()->virtual_membase();
  // The loader's std::string: 16 bytes inline or a pointer, then size and capacity.
  const uint8_t* const string = membase + r1.u32 + 96;
  const uint32_t size = LoadGuestWord(string + 16);
  const uint8_t* const text =
      LoadGuestWord(string + 20) >= 16 ? membase + LoadGuestWord(string) : string;
  const std::string_view name(reinterpret_cast<const char*>(text), size);
  const std::optional<TilingGroup> group = GroupOf(name);
  if (!group) {
    return;
  }
  ScenarioFile file{r31.u32, *group, {}};
  const uint32_t mask = LoadGuestWord(membase + r31.u32 + kScenarioFileMaskOffset);
  for (const uint32_t id : kTilingIds) {
    if (mask & (1u << id)) {
      file.tiling[id] = LoadGuestWord(ScenarioControlValue(membase, r31.u32, id));
    }
  }
  REXLOG_INFO("Render scenario {}: {} MSAA, tiling {} / UI {}", name,
              *group == TilingGroup::kRace ? "race" : "showroom",
              file.tiling[kTilingScenario] ? int(*file.tiling[kTilingScenario]) : -1,
              file.tiling[kUITilingScenario] ? int(*file.tiling[kUITilingScenario]) : -1);
  ApplyScenarioFileTiling(membase, file);
  const std::lock_guard lock(g_scenario_files_mutex);
  g_scenario_files.push_back(file);
}

void Fm4ScenarioPush(PPCRegister& r4) {
  FM4_HOOK_CENSUS();
  uint8_t* const membase = rex::system::kernel_state()->memory()->virtual_membase();
  const uint32_t address = LoadGuestWord(membase + r4.u32);
  const std::lock_guard lock(g_scenario_files_mutex);
  for (const ScenarioFile& file : g_scenario_files) {
    if (file.address == address) {
      ApplyScenarioFileTiling(membase, file);
      if (file.tiling[kTilingScenario]) {
        REXLOG_INFO("Render scenario {:08X} pushed: {} MSAA, tiling {}{}", address,
                    file.group == TilingGroup::kRace ? "race" : "showroom",
                    LoadGuestWord(ScenarioControlValue(membase, address, kTilingScenario)),
                    rex::ui::IsTemporalUpscalerActive() ? " (temporal upscaler)" : "");
      }
      return;
    }
  }
}

// The clip rectangle (left, top, right, bottom at r1 + 0xE0) of a recorded
// command buffer, made from the viewport at r1 + 0x380 without its origin: put
// the origin back, as the viewport's own draws have it.
void Fm4CommandBufferClipRect(PPCRegister& r1) {
  FM4_HOOK_CENSUS();
  uint8_t* const membase = rex::system::kernel_state()->memory()->virtual_membase();
  const uint8_t* const viewport = membase + r1.u32 + 0x380;
  uint8_t* const rect = membase + r1.u32 + 0xE0;
  const uint32_t x = LoadGuestWord(viewport);
  const uint32_t y = LoadGuestWord(viewport + 4);
  StoreGuestWord(rect, x);
  StoreGuestWord(rect + 4, y);
  StoreGuestWord(rect + 8, x + LoadGuestWord(viewport + 8));
  StoreGuestWord(rect + 12, y + LoadGuestWord(viewport + 12));
}

// The keys tabs name each pad button by what the first player's pad layout makes it do.
void Fm4ControllerLayoutApplied(PPCRegister& r4, PPCRegister& r5, PPCRegister& r31) {
  FM4_HOOK_CENSUS();
  const uint8_t* const player =
      rex::system::kernel_state()->memory()->virtual_membase() + r31.u32;
  const uint32_t index = LoadGuestWord(player + 0x1C);
  REXLOG_INFO("Controller layout: player {} device {} layout {}", index, r5.u32, r4.u32);
  if (index == 0 && r5.u32 == 0) {
    fm4_static_recomp::input::SetControllerLayout(r4.u32);
  }
}

namespace {

// From a thumbnail's render until the game has saved it, every resolve is read
// back: the readback rules miss some of the captures the CPU lights the car
// from (re-rendered past a run's first frames, or into a range no longer new).
// It reads the result later than the render ends.
enum class ThumbnailReadback { kNone, kPending, kSaved };
std::atomic<ThumbnailReadback> g_thumbnail_readback{ThumbnailReadback::kNone};

// The render draws the final car on the first tick its studio track and car are
// ready, but the car's ambient light is still the one the CPU computed from
// captures of the studio before its streaming thread filled it in: a flat white
// car. The console's timing always gets a fresh capture in first; a streaming
// thread that starts late here does not (1 launch in 10; every launch with the
// thread held back 500 ms, none with 3 ticks or more of this wait).
constexpr int kThumbnailSettleTicks = 12;
std::atomic<int> g_thumbnail_ready_ticks{0};

void UpdateThumbnailReadbacks() {
  const bool all =
      g_thumbnail_readback.load(std::memory_order_relaxed) != ThumbnailReadback::kNone;
  static bool applied = false;
  if (applied != all) {
    applied = all;
    rex::cvar::SetFlagByName("fh1_readback_all_resolves_hold", all ? "true" : "false");
    REXLOG_INFO("Thumbnail readbacks {}", all ? "all" : "back to the rules");
  }
}

void SetThumbnailRender(bool render) {
  g_thumbnail_render.store(render, std::memory_order_relaxed);
  REXLOG_INFO("Thumbnail render {}", render ? "started" : "done");
  // The scene's tiling is read every frame, so the thumbnail's own applies at once.
  {
    uint8_t* const membase = rex::system::kernel_state()->memory()->virtual_membase();
    const std::lock_guard lock(g_scenario_files_mutex);
    for (const ScenarioFile& file : g_scenario_files) {
      ApplyScenarioFileTiling(membase, file);
    }
  }
  if (render) {
    g_thumbnail_ready_ticks.store(0, std::memory_order_relaxed);
    g_thumbnail_readback.store(ThumbnailReadback::kPending, std::memory_order_relaxed);
    UpdateThumbnailReadbacks();
  }
}

}  // namespace

void Fm4ThumbnailRenderStart() {
  FM4_HOOK_CENSUS();
  SetThumbnailRender(true);
}

void Fm4ThumbnailSettle(PPCRegister& r3) {
  FM4_HOOK_CENSUS();
  if (g_thumbnail_ready_ticks.fetch_add(1, std::memory_order_relaxed) < kThumbnailSettleTicks) {
    r3.u64 = 0;
  }
}

void Fm4ThumbnailRenderDone() {
  FM4_HOOK_CENSUS();
  SetThumbnailRender(false);
}

namespace {
std::atomic<uint32_t> g_fm4_music_manager{0};

int FindMusicSlot(uint32_t stream) {
  const uint32_t manager = g_fm4_music_manager.load(std::memory_order_acquire);
  if (!manager) return -1;
  const auto* base = rex::system::kernel_state()->memory()->virtual_membase();
  for (size_t slot = 0; slot < 4; ++slot) {
    if (LoadGuestWord(base + manager + 0x4C + 4 * slot) == stream) return static_cast<int>(slot);
  }
  return -1;
}
}

void Fm4MusicUpdate(PPCRegister& r31) {
  FM4_HOOK_CENSUS();
  if (!fm4_static_recomp::CustomMusicEnabled()) return;
  g_fm4_music_manager.store(r31.u32, std::memory_order_release);
  const auto* base = rex::system::kernel_state()->memory()->virtual_membase();
  std::array<fm4_static_recomp::MusicStream, 4> streams{};
  for (size_t slot = 0; slot < streams.size(); ++slot) {
    auto& stream = streams[slot];
    stream.object = LoadGuestWord(base + r31.u32 + 0x4C + 4 * slot);
    if (!stream.object) continue;
    stream.playback = LoadGuestWord(base + stream.object + 0x0C);
    stream.volume = std::bit_cast<float>(LoadGuestWord(base + stream.object + 0x1C));
    stream.paused = base[stream.object + 0x14] != 0;
  }
  fm4_static_recomp::ObserveMusicStreams(r31.u32, streams);
}

void Fm4MusicStreamGain(PPCRegister& r31, PPCRegister& f1) {
  FM4_HOOK_CENSUS();
  if (!fm4_static_recomp::CustomMusicEnabled()) return;
  const int slot = FindMusicSlot(r31.u32);
  if (slot >= 0 && fm4_static_recomp::ReplacesMusicSlot(slot)) f1.f64 = 0.0;
}

void Fm4MusicStreamPlaying(PPCRegister& r31, PPCRegister& r3) {
  FM4_HOOK_CENSUS();
  if (!fm4_static_recomp::CustomMusicEnabled()) return;
  const int slot = FindMusicSlot(r31.u32);
  bool playing = false;
  if (slot >= 0 && fm4_static_recomp::CustomMusicPlaying(slot, r31.u32, playing)) {
    r3.u64 = playing ? 1 : 0;
  }
}

void Fm4MusicMovieStarted(PPCRegister& r31) {
  FM4_HOOK_CENSUS();
  fm4_static_recomp::SetCustomMusicMoviePlaying(r31.u32, true);
}

void Fm4MusicMovieStopped(PPCRegister& r31) {
  FM4_HOOK_CENSUS();
  fm4_static_recomp::SetCustomMusicMoviePlaying(r31.u32, false);
}

void Fm4MainLoopSession(PPCRegister& r11) {
  FM4_HOOK_CENSUS();
  const bool race = r11.u32 != 0;
  g_race_session.store(race, std::memory_order_relaxed);
  fm4_static_recomp::input::GameInputContext().SetRaceSession(race);
  fm4_static_recomp::SetCustomMusicRaceSession(race);
  fm4_static_recomp::UpdateCustomMusic();
  {
    const std::lock_guard lock(g_input_tick_mutex);
    if (g_input_tick) {
      g_input_tick();
    }
  }
  // The menus light their car from captures the CPU reads and keep the result, so a
  // capture read before its asynchronous readback lands tints the car for good.
  static bool held = false;
  if (held == race) {
    held = !race;
    rex::cvar::SetFlagByName("fh1_async_readbacks_hold", held ? "true" : "false");
    REXLOG_INFO("Async readbacks {} ({})", held ? "held" : "released", race ? "race" : "menus");
  }
  // The game opens a thumbnail's file and writes it within one tick.
  ThumbnailReadback saved = ThumbnailReadback::kSaved;
  g_thumbnail_readback.compare_exchange_strong(saved, ThumbnailReadback::kNone,
                                               std::memory_order_relaxed);
  UpdateThumbnailReadbacks();
  // Every tick, so the values are in place before a race loads its cars.
  ApplyRenderScenarioSettings();
  // No cvar change tells when the presenter starts or stops a temporal upscaler.
  if (g_switches_own_override.load(std::memory_order_relaxed) &&
      ReplayBlur() != g_applied_replay_blur.load(std::memory_order_relaxed)) {
    ApplyPixelShaderConstantOverride();
  }
}

void Fm4RaceMenuSceneCreated(PPCRegister& r3) {
  FM4_HOOK_CENSUS();
  fm4_static_recomp::input::GameInputContext().RaceMenuSceneCreated(r3.u32);
  REXLOG_INFO("Race menu scene created {:08X}", r3.u32);
}

void Fm4RaceSubmenuSceneCreated(PPCRegister& r3) {
  FM4_HOOK_CENSUS();
  fm4_static_recomp::input::GameInputContext().RaceSubmenuSceneCreated(r3.u32);
  REXLOG_INFO("Race submenu scene created {:08X}", r3.u32);
}

void Fm4RaceMenuSceneDestroyed(PPCRegister& r3) {
  FM4_HOOK_CENSUS();
  fm4_static_recomp::input::GameInputContext().RaceMenuSceneDestroyed(r3.u32);
  REXLOG_INFO("Race menu scene destroyed {:08X}", r3.u32);
}

bool Fm4SkipVblankCallback() {
  FM4_HOOK_CENSUS();
  // Runs on every vblank, so a new rate applies from the next one. A race waking
  // on every second vblank of one at twice its rate is no substitute: a frame then
  // flips on whichever vblank follows its swap, and frames whose swap falls near
  // the one in between alternate between half and one and a half frame periods.
  const bool fast_load = g_fast_load.load(std::memory_order_relaxed);
  const bool race = g_race_session.load(std::memory_order_relaxed);
  const bool console = REXCVAR_GET(fm4_console_rate);
  const int32_t debug_vblank_hz = REXCVAR_GET(fm4_debug_vblank_hz);
  const double vblank_hz = fast_load ? kLoadVblankHz
                           : console ? kConsoleVblankHz
                           : debug_vblank_hz > 0 ? double(debug_vblank_hz)
                                                : double(REXCVAR_GET(fm4_frame_rate));
  static double applied_hz = 0.0;
  if (vblank_hz != applied_hz) {
    applied_hz = vblank_hz;
    rex::cvar::SetFlagByName(kVblankHzOption, std::to_string(vblank_hz));
  }
  const int32_t rate = REXCVAR_GET(fm4_menu_vblank_hz);
  if (fast_load || race || console || rate <= 0) {
    return false;
  }
  // Counting vblanks cannot lose one to timer jitter the way a time schedule
  // does when the two rates are equal.
  static uint32_t vblank_index = 0;
  const uint32_t divisor = std::max(1u, uint32_t(std::lround(vblank_hz / rate)));
  return ++vblank_index % divisor != 0;
}

namespace fm4_static_recomp {

void InstallPixelShaderSwitches() {
  rex::cvar::SetFlagByName("gpu_wrap_seam_vertex_shaders", DirtVertexShaderList());
  rex::cvar::SetFlagByName("gpu_wrap_seam_pixel_shaders", DirtPixelShaderList());
  rex::cvar::SetFlagByName("gpu_wrap_seam_fix", "true");
  // An override given at launch is a diagnostic and wins over MOTION BLUR and CAR DIRT's OFF
  // and the replay blur's.
  const bool own_override =
      SwitchesOwnOverride(rex::cvar::GetFlagByName("gpu_pixel_shader_constant_override"));
  if (own_override) {
    g_switches_own_override.store(true, std::memory_order_relaxed);
    ApplyPixelShaderConstantOverride();
    for (const char* name : {"fm4_road_detail_blur", "fm4_car_dirt", "fm4_upscaler_replay_blur"}) {
      rex::cvar::RegisterChangeCallback(name, [](std::string_view, std::string_view) {
        ApplyPixelShaderConstantOverride();
      });
    }
  } else {
    REXLOG_INFO("fm4 switches: inactive, gpu_pixel_shader_constant_override is set");
  }
}

void SetPromptTextSource(
    std::function<std::optional<std::u16string>(uint16_t key, std::u16string_view text)> source) {
  const std::lock_guard lock(g_prompt_text_mutex);
  g_prompt_text_source = std::move(source);
}

void SetInputTick(std::function<void()> tick) {
  const std::lock_guard lock(g_input_tick_mutex);
  g_input_tick = std::move(tick);
}

void SetGraphicsPageOpener(std::function<void()> opener) {
  const std::lock_guard lock(g_graphics_page_mutex);
  g_graphics_page_opener = std::move(opener);
}

void NoteGuestFileOpen(std::string_view guest_path) {
  constexpr std::string_view kThumbnailFile = "\\thumbnails\\thumbnail_";
  // The garage opens saved thumbnails to show them too.
  if (g_thumbnail_render.load(std::memory_order_relaxed) ||
      g_thumbnail_readback.load(std::memory_order_relaxed) != ThumbnailReadback::kPending ||
      !std::ranges::search(guest_path, kThumbnailFile, [](char a, char b) {
         return std::tolower(static_cast<unsigned char>(a)) == b;
       })) {
    return;
  }
  g_thumbnail_readback.store(ThumbnailReadback::kSaved, std::memory_order_relaxed);
  REXLOG_INFO("Thumbnail saved: {}", guest_path);
}

}  // namespace fm4_static_recomp

// The end of the Options items in My Profile's menu builder (r30 = the Options
// category, r31 = the "MyProfile" table name): PC OPTIONS after them, added as
// the game adds each item, through the frame's own two temporaries, which the
// last item has just released.
void Fm4AddGraphicsOption(PPCContext& ctx, uint8_t* base, PPCRegister& r1, PPCRegister& r30,
                          PPCRegister& r31) {
  FM4_HOOK_CENSUS();
  const uint32_t string_id = r1.u32 + 0xA0;
  const uint32_t label = r1.u32 + 0xA4;
  CallGuest(ctx, base, r1.u32, kMakeLocString, {string_id, r31.u32, kGraphicsStringKey});
  CallGuest(ctx, base, r1.u32, kMakeMenuLabel, {label, string_id});
  CallGuest(ctx, base, r1.u32, kMenuAddItem, {r30.u32, label, kGraphicsAction, 1, 0});
  // AddItem keeps the label's reference; the string id's is released.
  if (const uint32_t object = LoadGuestWord(base, string_id)) {
    const uint32_t release = LoadGuestWord(base, LoadGuestWord(base, object) + 8);
    CallGuest(ctx, base, r1.u32, release, {object});
  }
}

// The string table lookup (table, key): PC OPTIONS for the entry's key, and the
// strings that name pad buttons as the prompt text source rewrites them.
bool Fm4GraphicsOptionText(PPCContext& ctx, uint8_t* base, PPCRegister& r1, PPCRegister& r3,
                           PPCRegister& r4) {
  FM4_HOOK_CENSUS();
  const uint16_t key = uint16_t(r4.u32 & 0xFFFF);
  if (key == kGraphicsStringKey) {
    static const uint32_t text = GuestText(u"PC OPTIONS");
    r3.u64 = text;
    return true;
  }
  // The nested lookup below runs this hook again.
  thread_local bool nested = false;
  if (nested) {
    return false;
  }
  const bool log = fm4_static_recomp::StringLogEnabled();
  std::function<std::optional<std::u16string>(uint16_t, std::u16string_view)> source;
  if (fm4_static_recomp::input::IsPromptStringKey(key)) {
    const std::lock_guard lock(g_prompt_text_mutex);
    source = g_prompt_text_source;
  }
  if (!source && !log) {
    return false;
  }
  nested = true;
  const uint32_t original = CallGuest(ctx, base, r1.u32, kStringLookup, {r3.u32, r4.u32});
  nested = false;
  r3.u64 = original;
  if (!original) {
    return true;
  }
  const std::u16string found = ReadGuestText(base, original);
  if (log) {
    const double now = std::chrono::duration<double>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    const std::lock_guard lock(g_string_log_mutex);
    if (g_string_log.Shown(found, now)) {
      REXLOG_INFO("{}", fm4_static_recomp::StringLogLine(found));
    }
  }
  if (!source) {
    return true;
  }
  std::u16string shown = found;
  if (const auto text = source(key, found)) {
    const std::lock_guard lock(g_prompt_text_mutex);
    uint32_t& address = g_prompt_texts[*text];
    if (!address) {
      address = GuestText(*text);
      REXLOG_INFO("Prompt text {:04X}: {}", key, std::string(text->begin(), text->end()));
    }
    r3.u64 = address;
    shown = *text;
  }
  // A scene's static text: the element being built is the caller's r31.
  if (uint32_t(ctx.lr) == kTextSetupGetTextReturn) {
    const std::lock_guard lock(g_live_prompt_mutex);
    g_live_prompts[ctx.r31.u32] = {key, found, std::move(shown)};
  }
  return true;
}

// A text element's update, before it checks whether to lay its text out again: a prompt
// text built for the other device is rewritten, as the game's SetText would. The element
// is live here; one that no longer shows the text it was given is another element now.
void Fm4TextElementUpdate(PPCContext& ctx, uint8_t* base, PPCRegister& r1, PPCRegister& r30) {
  FM4_HOOK_CENSUS();
  const uint32_t element = r30.u32;
  LivePromptText live;
  {
    const std::lock_guard lock(g_live_prompt_mutex);
    const auto found = g_live_prompts.find(element);
    if (found == g_live_prompts.end()) {
      return;
    }
    live = found->second;
  }
  std::function<std::optional<std::u16string>(uint16_t, std::u16string_view)> source;
  {
    const std::lock_guard lock(g_prompt_text_mutex);
    source = g_prompt_text_source;
  }
  if (!source) {
    return;
  }
  const std::u16string wanted = source(live.key, live.original).value_or(live.original);
  if (wanted == live.shown) {
    if (live.relayout) {
      base[element + kTextElementLayout] = 1;
      const std::lock_guard lock(g_live_prompt_mutex);
      g_live_prompts[element].relayout = false;
    }
    return;
  }
  if (ReadElementText(base, element) != live.shown) {
    const std::lock_guard lock(g_live_prompt_mutex);
    g_live_prompts.erase(element);
    return;
  }
  uint32_t text;
  {
    const std::lock_guard lock(g_prompt_text_mutex);
    uint32_t& address = g_prompt_texts[wanted];
    if (!address) {
      address = GuestText(wanted);
    }
    text = address;
  }
  CallGuest(ctx, base, r1.u32, kTextAssign,
            {element + kTextElementString, text, uint32_t(wanted.size())});
  base[element + kTextElementLayout] = 1;
  const std::lock_guard lock(g_live_prompt_mutex);
  g_live_prompts[element].shown = wanted;
  g_live_prompts[element].relayout = true;
}

// The Options dispatcher (this, action): PC OPTIONS opens the host page; the
// game's own actions go on as before.
bool Fm4OpenGraphicsOption(PPCRegister& r4) {
  FM4_HOOK_CENSUS();
  if (r4.u32 != kGraphicsAction) {
    return false;
  }
  const std::lock_guard lock(g_graphics_page_mutex);
  if (g_graphics_page_opener) {
    g_graphics_page_opener();
  }
  return true;
}

REXCVAR_DEFINE_BOOL(fm4_audio_compatibility, false, "Forza Motorsport 4",
                    "Opt-in faster, result-preserving audio mixer math for processors without FMA")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// FMOD mixes in 256-sample blocks and starts, stops and re-seeks voices between them without a
// fade, so wherever that happens to a loud voice the mix steps at the block's first samples: the
// clicks while revving at the limiter and shifting, where the top engine layers and the turbo are
// restarted every few blocks (the same on a console). The render thread lays each mixed block out
// as 6 channels of 256 big-endian floats and hands it to 0x82C56B78 before submitting it; there,
// a step at a block's start well above the channel's own sample-to-sample movement is taken out:
// its first samples are bridged from the block before, and the offset that is left fades out over
// the next 128 samples. AUDIO POP FIX in the launcher's SETTINGS switches this and the decoder's
// loop crossfade (xma_loop_end_crossfade, see InstallAudioPopFix) together.
REXCVAR_DEFINE_BOOL(fm4_audio_pop_fix, true, "Forza Motorsport 4",
                    "Smooth the pops of the game's audio, heard on a console too: crossfade each "
                    "loop of a sound into its start, and the steps FMOD leaves at the start of a "
                    "mix block when it starts, stops or re-seeks a voice while revving and shifting");

namespace {
constexpr uint32_t kMixChannels = 6, kMixBlock = 256;

struct MixEdgeState {
  float y1 = 0, y2 = 0;  // the last two samples of the block before
  float rough = 0;       // the RMS second difference of the block before
  bool valid = false;
};
std::array<MixEdgeState, kMixChannels> g_mix_edges;

void DeclickBlockStart(float* x, MixEdgeState& state) {
  constexpr uint32_t kBridge = 4, kFade = 128;
  constexpr float kThreshold = 6.0f, kFloor = 0.003f;
  if (state.valid) {
    float prev2 = state.y2, prev1 = state.y1, peak = 0;
    for (uint32_t i = 0; i <= kBridge; i++) {
      peak = std::max(peak, std::fabs(x[i] - 2.0f * prev1 + prev2));
      prev2 = prev1;
      prev1 = x[i];
    }
    if (peak > kThreshold * state.rough && peak > kFloor) {
      // The step: where the block is once past the bridge, against where the block before was
      // heading (the trend of both sides averaged across the gap).
      constexpr float span = float(kBridge + 1);
      const float slope_before = state.y1 - state.y2, slope_after = x[kBridge + 1] - x[kBridge];
      const float step = x[kBridge] - state.y1 - span * 0.5f * (slope_before + slope_after);
      const float end = x[kBridge] - step;
      for (uint32_t i = 0; i < kBridge; i++) {
        const float u = float(i + 1) / span, u2 = u * u, u3 = u2 * u;
        x[i] = (2 * u3 - 3 * u2 + 1) * state.y1 + (u3 - 2 * u2 + u) * span * slope_before +
               (-2 * u3 + 3 * u2) * end + (u3 - u2) * span * slope_after;
      }
      for (uint32_t i = 0; i < kFade; i++) {
        const float t = float(i) / float(kFade);
        x[kBridge + i] -= step * (2 * t * t * t - 3 * t * t + 1);
      }
    }
  }
  float sum = 0;
  for (uint32_t i = 2; i < kMixBlock; i++) {
    const float e = x[i] - 2.0f * x[i - 1] + x[i - 2];
    sum += e * e;
  }
  state.rough = std::sqrt(sum / float(kMixBlock - 2));
  state.y1 = x[kMixBlock - 1];
  state.y2 = x[kMixBlock - 2];
  state.valid = true;
}
}  // namespace

void Fm4AudioMixBlock(PPCContext&, uint8_t* base, PPCRegister& r4) {
  FM4_HOOK_CENSUS();
  const uint32_t frame = r4.u32;
  if (REXCVAR_GET(fm4_audio_pop_fix) && frame) {
    for (uint32_t c = 0; c < kMixChannels; c++) {
      uint8_t* const samples = base + frame + c * kMixBlock * 4;
      std::array<float, kMixBlock> x;
      for (uint32_t i = 0; i < kMixBlock; i++) {
        x[i] = std::bit_cast<float>(LoadGuestWord(samples + 4 * i));
      }
      DeclickBlockStart(x.data(), g_mix_edges[c]);
      for (uint32_t i = 0; i < kMixBlock; i++) {
        StoreGuestWord(samples + 4 * i, std::bit_cast<uint32_t>(x[i]));
      }
    }
  } else {
    for (MixEdgeState& state : g_mix_edges) state.valid = false;
  }
}

namespace fm4_static_recomp {

void InstallAudioCompatibility() {
  rex::ppc::g_exact_single_fma.store(REXCVAR_GET(fm4_audio_compatibility), std::memory_order_relaxed);
  REXLOG_INFO("Audio compatibility: {}", REXCVAR_GET(fm4_audio_compatibility));
}

void InstallAudioPopFix() {
  // A launch option for the crossfade is a diagnostic and wins over AUDIO POP FIX.
  constexpr const char* kCrossfade = "xma_loop_end_crossfade";
  if (!rex::cvar::GetFlagInfo(kCrossfade)) {
    REXLOG_WARN("Audio pop fix: the runtime has no {}; AUDIO POP FIX smooths the mix blocks only",
                kCrossfade);
    return;
  }
  if (rex::cvar::GetFlagSource(kCrossfade) == rex::cvar::Source::kCommandLine) {
    REXLOG_INFO("Audio pop fix: {} comes from the command line", kCrossfade);
    return;
  }
  const std::string value = rex::cvar::GetFlagByName("fm4_audio_pop_fix");
  if (!rex::cvar::SetFlagByName(kCrossfade, value)) {
    REXLOG_ERROR("Audio pop fix: {} refused {}", kCrossfade, value);
  }
  REXLOG_INFO("Audio pop fix: {}", value);
}

}  // namespace fm4_static_recomp

bool Fm4AudioStableGain(PPCContext&, uint8_t* base, PPCRegister& r21, PPCRegister& r23,
                       PPCRegister& r27, PPCRegister& r28, PPCRegister& r31) {
  if (!rex::ppc::g_exact_single_fma.load(std::memory_order_relaxed)) return false;
  const uint32_t processed = fm4_static_recomp::ProcessStableAudioGain(
      base + r31.u32, base + r28.u32, base + uint32_t(r28.u32 + r27.u32), r21.u32, r23.u32);
  r21.u64 -= processed;
  r28.u64 += uint64_t(processed) * r23.u32 * 4;
  return r21.u32 == 0;
}
