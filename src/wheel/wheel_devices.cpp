// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
#include "wheel/wheel_devices.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#include <fmt/format.h>
#include <rex/logging.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fm4_static_recomp::wheel {

namespace ffb = rex::input::xinputd_ffb;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {

constexpr int kSlots = 5;  // the game's effect slots, ids 1 to 5
// Updates keep the latest value at most this often: a constant force follows the road, the
// conditions follow the car's speed.
constexpr auto kConstantInterval = 10ms;
constexpr auto kConditionInterval = 40ms;
// The game sends effects every frame while the player drives; after this long without any, the
// forces stop (a pause, a menu, a crash of the game's own wheel code).
constexpr auto kHeartbeat = 200ms;
// How often a running effect is started again, in case the driver stopped it.
constexpr auto kStatusInterval = 1s;
constexpr auto kRumbleInterval = 20ms;
// How often the force device is opened again while effect calls keep failing.
constexpr auto kReopenInterval = 2s;

bool WindowFocused() {
#if defined(_WIN32)
  DWORD process = 0;
  if (HWND foreground = GetForegroundWindow()) {
    GetWindowThreadProcessId(foreground, &process);
  }
  return process == GetCurrentProcessId();
#else
  return true;
#endif
}

bool BuildEffect(const ffb::Effect& source, const Settings& s, SDL_HapticEffect& out) {
  out = {};
  switch (source.type) {
    case ffb::kConstant:
      out.type = SDL_HAPTIC_CONSTANT;
      out.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
      out.constant.length = SDL_HAPTIC_INFINITY;
      out.constant.level = ConstantLevel(source, s);
      return true;
    case ffb::kSpring:
      out.type = SDL_HAPTIC_SPRING;
      break;
    case ffb::kDamper:
      out.type = SDL_HAPTIC_DAMPER;
      break;
    case ffb::kInertia:
      out.type = SDL_HAPTIC_INERTIA;
      break;
    case ffb::kFriction:
      out.type = SDL_HAPTIC_FRICTION;
      break;
    default:
      return false;
  }
  const auto c = ConditionValues(source, s);
  out.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;
  out.condition.length = SDL_HAPTIC_INFINITY;
  out.condition.right_coeff[0] = c.right_coeff;
  out.condition.left_coeff[0] = c.left_coeff;
  out.condition.right_sat[0] = c.right_sat;
  out.condition.left_sat[0] = c.left_sat;
  out.condition.deadband[0] = c.deadband;
  out.condition.center[0] = c.center;
  return true;
}

uint32_t FeatureFor(uint32_t type) {
  switch (type) {
    case ffb::kConstant:
      return SDL_HAPTIC_CONSTANT;
    case ffb::kSpring:
      return SDL_HAPTIC_SPRING;
    case ffb::kDamper:
      return SDL_HAPTIC_DAMPER;
    case ffb::kInertia:
      return SDL_HAPTIC_INERTIA;
    case ffb::kFriction:
      return SDL_HAPTIC_FRICTION;
    default:
      return 0;
  }
}

}  // namespace

struct DeviceMonitor::Impl {
  // Shared with the callers, under `mutex`.
  struct Slot {
    ffb::Effect effect;
    bool known = false, dirty = false, run = false;
    uint32_t updates = 0;  // calls from the game, for the diagnostics
  };
  mutable std::mutex mutex;
  Snapshot published;
  std::array<Slot, kSlots + 1> slots;
  bool clear = false, driving = false;
  Clock::time_point heartbeat{};
  uint16_t rumble_left = 0, rumble_right = 0;
  std::optional<Settings> test_request;
  bool test_rumble_request = false;

  // The worker's own.
  struct Open {
    SDL_Joystick* joystick = nullptr;
    Device device;
  };
  struct Played {
    SDL_HapticEffectID id = -1;
    uint32_t type = 0;
    bool running = false;
    Clock::time_point updated{}, checked{};
    SDL_HapticEffect last{};  // what the device has, to send only changes
  };
  const Settings settings;
  const Composer composer;
  const bool ignore_driving = settings.ffb_ignore_driving;
  const bool launcher;
  std::vector<Open> open;
  uint64_t generation = 0;
  bool ready = false;
  SDL_Haptic* haptic = nullptr;
  SDL_JoystickID haptic_owner = 0;
  uint32_t features = 0;
  std::string ffb_name, haptic_status = "no force feedback device";
  std::array<Played, kSlots + 1> played;
  SDL_HapticEffectID rumble_id = -1, test_id = -1;
  bool rumble_running = false;
  SDL_HapticEffect rumble_last{};
  Clock::time_point rumble_at{};
  bool output_on = false;
  // Diagnostics: why the forces are off, effect updates since the last stats line, SDL errors.
  std::string gate_reason = "starting";
  std::array<uint32_t, kSlots + 1> updates_seen{}, sent{}, restarts{};
  Clock::time_point stats_at{};
  int errors_logged = 0;
  // An effect call failed (the driver lost the device, or kept an old session's effects):
  // the force device is opened again, which resets it, at most this often.
  bool failed = false;
  Clock::time_point reopened_at{};
  uint32_t reopens = 0;
  int test_stage = 0;
  bool test_rumble = false;
  Clock::time_point test_next{};
  Settings test_settings;

  std::atomic<bool> quit{false};
  std::thread worker;

  Impl(Settings s, bool launcher_mode)
      : settings(std::move(s)), composer(settings), launcher(launcher_mode) {
    worker = std::thread([this] { Run(); });
  }
  ~Impl() {
    quit = true;
    if (worker.joinable()) {
      worker.join();
    }
  }

  void Run() {
    // The runtime's SDL owns raw input for its gamepads; this one reads wheels through
    // DirectInput, XInput and HIDAPI, and keeps reading while the window is in the background
    // (the game decides what it uses).
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC)) {
      const std::string error = SDL_GetError();
      REXLOG_ERROR("Wheel: SDL_Init failed: {}", error);
      {
        std::lock_guard lock(mutex);
        published.ready = true;
        published.haptic_status = "SDL could not start: " + error;
      }
      while (!quit) {
        std::this_thread::sleep_for(50ms);
      }
      return;
    }
    while (!quit) {
      SDL_PumpEvents();
      SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
      Refresh();
      Snapshot snap = Build();
      if (launcher) {
        Test(snap);
      } else {
        Force(snap);
      }
      snap.ffb_name = ffb_name;
      snap.haptic_status = haptic_status;
      snap.force_supported = haptic && (features & SDL_HAPTIC_CONSTANT);
      snap.rumble_supported = haptic && (features & SDL_HAPTIC_SINE);
      snap.test_busy = test_stage != 0;
      {
        std::lock_guard lock(mutex);
        published = std::move(snap);
      }
      std::this_thread::sleep_for(4ms);
    }
    CloseHaptic();
    for (Open& o : open) {
      SDL_CloseJoystick(o.joystick);
    }
    open.clear();
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC);
  }

  void Refresh() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    std::vector<SDL_JoystickID> now(ids, ids + (ids ? count : 0));
    SDL_free(ids);
    const size_t before = open.size();
    std::erase_if(open, [&](Open& o) {
      if (std::find(now.begin(), now.end(), o.device.id) != now.end()) {
        return false;
      }
      if (haptic && haptic_owner == o.device.id) {
        CloseHaptic();
      }
      SDL_CloseJoystick(o.joystick);
      return true;
    });
    bool changed = open.size() != before;
    for (const SDL_JoystickID id : now) {
      if (std::any_of(open.begin(), open.end(), [&](const Open& o) { return o.device.id == id; })) {
        continue;
      }
      SDL_Joystick* joystick = SDL_OpenJoystick(id);
      if (!joystick) {
        continue;
      }
      Open o;
      o.joystick = joystick;
      Device& d = o.device;
      d.id = id;
      const char* name = SDL_GetJoystickName(joystick);
      const char* path = SDL_GetJoystickPath(joystick);
      d.name = name ? name : "";
      d.path = path ? path : "";
      d.vendor = SDL_GetJoystickVendor(joystick);
      d.product = SDL_GetJoystickProduct(joystick);
      d.wheel = SDL_GetJoystickType(joystick) == SDL_JOYSTICK_TYPE_WHEEL;
      d.axes.assign(std::max(SDL_GetNumJoystickAxes(joystick), 0), 0);
      d.buttons.assign(std::max(SDL_GetNumJoystickButtons(joystick), 0), false);
      d.hats.assign(std::max(SDL_GetNumJoystickHats(joystick), 0), 0);
      Read(o);
      d.rest = d.axes;
      open.push_back(std::move(o));
      changed = true;
    }
    for (Open& o : open) {
      Read(o);
    }
    if (changed || !ready) {
      ++generation;
      ready = true;
    }
  }

  static void Read(Open& o) {
    Device& d = o.device;
    for (size_t i = 0; i < d.axes.size(); ++i) {
      d.axes[i] = SDL_GetJoystickAxis(o.joystick, int(i));
    }
    for (size_t i = 0; i < d.buttons.size(); ++i) {
      d.buttons[i] = SDL_GetJoystickButton(o.joystick, int(i));
    }
    for (size_t i = 0; i < d.hats.size(); ++i) {
      d.hats[i] = SDL_GetJoystickHat(o.joystick, int(i));
    }
  }

  Snapshot Build() const {
    Snapshot snap;
    snap.devices.reserve(open.size());
    for (const Open& o : open) {
      snap.devices.push_back(o.device);
    }
    snap.generation = generation;
    snap.ready = ready;
    return snap;
  }

  SDL_Joystick* JoystickOf(const Device* device) const {
    for (const Open& o : open) {
      if (device && o.device.id == device->id) {
        return o.joystick;
      }
    }
    return nullptr;
  }

  const Device* ForceTarget(const Snapshot& snap, const Settings& s) const {
    if (!s.ffb_device.empty()) {
      return FindDevice(snap.devices, s.ffb_device);
    }
    // The game's own settings are parsed once; a test brings the launcher's.
    return &s == &settings ? composer.Base(snap) : Composer(s).Base(snap);
  }

  // Opens the force device; false (with haptic_status saying why) when none can play forces.
  bool OpenHaptic(const Device* target) {
    if (haptic && target && haptic_owner == target->id) {
      return true;
    }
    CloseHaptic();
    if (!target) {
      haptic_status = "force feedback device not connected";
      return false;
    }
    ffb_name = target->name;
    SDL_Joystick* joystick = JoystickOf(target);
    haptic = joystick ? SDL_OpenHapticFromJoystick(joystick) : nullptr;
    if (!haptic) {
      haptic_status = std::string("no force feedback: ") + SDL_GetError();
      return false;
    }
    haptic_owner = target->id;
    features = SDL_GetHapticFeatures(haptic);
    if (features & SDL_HAPTIC_GAIN) {
      SDL_SetHapticGain(haptic, 100);
    }
    // The game's spring centres the wheel; the base's own would add to it.
    if (features & SDL_HAPTIC_AUTOCENTER) {
      SDL_SetHapticAutocenter(haptic, 0);
    }
    haptic_status = (features & SDL_HAPTIC_CONSTANT) ? "ready" : "no constant force";
    REXLOG_INFO("Wheel force feedback: \"{}\" opened, features {:#x}", ffb_name, features);
    return true;
  }

  void CloseHaptic() {
    if (!haptic) {
      return;
    }
    SDL_StopHapticEffects(haptic);
    for (Played& p : played) {
      if (p.id >= 0) {
        SDL_DestroyHapticEffect(haptic, p.id);
      }
      p = {};
    }
    if (rumble_id >= 0) {
      SDL_DestroyHapticEffect(haptic, rumble_id);
    }
    if (test_id >= 0) {
      SDL_DestroyHapticEffect(haptic, test_id);
    }
    rumble_id = test_id = -1;
    rumble_running = false;
    SDL_CloseHaptic(haptic);
    haptic = nullptr;
    haptic_owner = 0;
    features = 0;
    output_on = false;
  }

  void StopOutput() {
    if (!output_on || !haptic) {
      return;
    }
    SDL_StopHapticEffects(haptic);
    for (Played& p : played) {
      p.running = false;
    }
    rumble_running = false;
    output_on = false;
  }

  void Force(const Snapshot& snap) {
    const FfbMode mode = ParseFfbMode(settings.ffb_mode);
    if (mode == FfbMode::kOff) {
      CloseHaptic();
      haptic_status = "off";
      return;
    }
    if (failed && Clock::now() - reopened_at >= kReopenInterval) {
      failed = false;
      reopened_at = Clock::now();
      ++reopens;
      REXLOG_WARN("Wheel force feedback: an effect call failed; opening the force device again "
                  "(reopen {}). Close other programs that use the wheel (Thrustmaster Control "
                  "Panel, Game Controllers) while playing.", reopens);
      CloseHaptic();
    }
    if (!OpenHaptic(ForceTarget(snap, settings))) {
      return;
    }
    std::array<Slot, kSlots + 1> slots_now;
    bool clear_now, driving_now;
    Clock::time_point heartbeat_now;
    uint16_t left, right;
    {
      std::lock_guard lock(mutex);
      clear_now = std::exchange(clear, false);
      for (int i = 1; i <= kSlots; ++i) {
        slots_now[i] = slots[i];
        slots[i].dirty = false;
      }
      driving_now = driving;
      heartbeat_now = heartbeat;
      left = rumble_left;
      right = rumble_right;
    }
    if (clear_now) {
      SDL_StopHapticEffects(haptic);
      rumble_running = false;
      for (Played& p : played) {
        if (p.id >= 0) {
          SDL_DestroyHapticEffect(haptic, p.id);
        }
        p = {};
      }
    }
    const auto now = Clock::now();
    const char* reason = !WindowFocused()                    ? "the game window is in the background"
                         : !driving_now && !ignore_driving   ? "the game says the player is not driving"
                         : now - heartbeat_now > kHeartbeat ? "no effect from the game for 200 ms"
                                                             : "";
    if (reason != gate_reason) {
      gate_reason = reason;
      REXLOG_INFO("Wheel force feedback: {}", *reason ? std::string("off, ") + reason : "playing");
    }
    LogStats(slots_now, now);
    if (*reason) {
      StopOutput();
      return;
    }
    output_on = true;
    if (mode == FfbMode::kEffects) {
      for (int i = 1; i <= kSlots; ++i) {
        Play(i, slots_now[i], now);
      }
    }
    Rumble(RumbleMagnitude(left, right, settings), left, right);
  }

  void LogError(const char* what, int slot) {
    failed = true;
    if (errors_logged < 20) {
      ++errors_logged;
      REXLOG_WARN("Wheel force feedback: {} failed for effect {}: {}", what, slot, SDL_GetError());
    }
  }

  // Every 5 s: how many updates each effect had, what is playing and how strong.
  void LogStats(const std::array<Slot, kSlots + 1>& slots_now, Clock::time_point now) {
    if (now - stats_at < 5s) {
      return;
    }
    stats_at = now;
    std::string line;
    for (int i = 1; i <= kSlots; ++i) {
      const Slot& slot = slots_now[i];
      if (!slot.known) {
        continue;
      }
      const uint32_t updates = slot.updates - updates_seen[i];
      updates_seen[i] = slot.updates;
      const auto& e = slot.effect;
      line += fmt::format(" | #{} {} upd {} sent {} restarted {} run {}/{} gain {}", i, ffb::EffectTypeName(e.type),
                          updates, sent[i], restarts[i], slot.run ? 1 : 0, played[i].running ? 1 : 0, e.gain);
      sent[i] = restarts[i] = 0;
      if (e.type == ffb::kConstant) {
        line += fmt::format(" mag {} -> {}", e.magnitude, ConstantLevel(e, settings));
      } else {
        line += fmt::format(" coef {}/{} sat {}/{} -> {}", e.positive_coefficient,
                            e.negative_coefficient, e.positive_saturation, e.negative_saturation,
                            ConditionValues(e, settings).right_coeff);
      }
    }
    if (!line.empty()) {
      REXLOG_INFO("Wheel force feedback stats ({}){}", gate_reason.empty() ? "playing" : gate_reason,
                  line);
    }
  }

  void Play(int i, const Slot& slot, Clock::time_point now) {
    Played& p = played[i];
    // After a failed call nothing more is sent until the device is opened again: a driver that
    // refuses effects must not be asked hundreds of times a second.
    if (failed || !slot.known || !(features & FeatureFor(slot.effect.type))) {
      return;
    }
    const auto interval =
        slot.effect.type == ffb::kConstant ? kConstantInterval : kConditionInterval;
    if (slot.dirty || p.id < 0) {
      if (p.id >= 0 && p.type != slot.effect.type) {
        SDL_DestroyHapticEffect(haptic, p.id);
        p = {};
      }
      if (p.id < 0 || now - p.updated >= interval) {
        SDL_HapticEffect effect;
        if (BuildEffect(slot.effect, settings, effect)) {
          if (p.id < 0) {
            p.id = SDL_CreateHapticEffect(haptic, &effect);
            p.type = slot.effect.type;
            p.running = false;
            p.last = effect;
            if (p.id < 0) {
              LogError("creating", i);
            }
            ++sent[i];
            p.updated = now;
          } else if (std::memcmp(&effect, &p.last, sizeof(effect)) != 0) {
            // The game sends every effect every frame, mostly unchanged; some drivers
            // (Thrustmaster's) stop an effect that is reloaded that often.
            if (!SDL_UpdateHapticEffect(haptic, p.id, &effect)) {
              LogError("updating", i);
            } else if (p.running && !SDL_RunHapticEffect(haptic, p.id, 1)) {
              // Started again with its new values, as DirectInput's DIEP_START does:
              // Thrustmaster's driver (T150) otherwise stops playing an updated effect after a
              // while, while still reporting it as playing.
              LogError("restarting", i);
            }
            p.last = effect;
            ++sent[i];
            p.updated = now;
          }
        }
      } else {
        // Too soon: keep the slot dirty so the latest value goes out next time.
        std::lock_guard lock(mutex);
        slots[i].dirty = true;
      }
    }
    if (p.id < 0) {
      return;
    }
    if (slot.run && !p.running) {
      p.running = SDL_RunHapticEffect(haptic, p.id, 1);
      if (!p.running) {
        LogError("starting", i);
      }
    } else if (!slot.run && p.running) {
      SDL_StopHapticEffect(haptic, p.id);
      p.running = false;
    } else if (p.running && settings.ffb_periodic_restart && now - p.checked >= kStatusInterval) {
      // A driver may stop an effect on its own, and Thrustmaster's goes on reporting it as
      // playing: every running effect is started again now and then (seamless for these
      // endless effects; switching windows away and back did the same).
      p.checked = now;
      ++restarts[i];
      if (!SDL_RunHapticEffect(haptic, p.id, 1)) {
        LogError("restarting", i);
      }
    }
  }

  void Rumble(int16_t magnitude, uint16_t left, uint16_t right) {
    if (failed || !(features & SDL_HAPTIC_SINE)) {
      return;
    }
    const auto now = Clock::now();
    if (magnitude <= 0) {
      if (rumble_id >= 0 && rumble_running) {
        SDL_StopHapticEffect(haptic, rumble_id);
        rumble_running = false;
      }
      return;
    }
    SDL_HapticEffect effect{};
    effect.type = SDL_HAPTIC_SINE;
    effect.periodic.direction.type = SDL_HAPTIC_STEERING_AXIS;
    effect.periodic.length = SDL_HAPTIC_INFINITY;
    // The right (engine) motor buzzes, the left (road) one rumbles.
    effect.periodic.period = right > left ? 25 : 60;
    effect.periodic.magnitude = magnitude;
    if (rumble_id < 0) {
      rumble_id = SDL_CreateHapticEffect(haptic, &effect);
      if (rumble_id < 0) {
        LogError("creating", 0);
        return;
      }
      rumble_running = SDL_RunHapticEffect(haptic, rumble_id, 1);
      rumble_last = effect;
      rumble_at = now;
      return;
    }
    // Like the game's effects: sent only when it changes, at most every 20 ms, and started
    // again with it (and once a second) for Thrustmaster's driver.
    const bool changed = std::memcmp(&effect, &rumble_last, sizeof(effect)) != 0;
    if ((changed && now - rumble_at >= kRumbleInterval) || !rumble_running ||
        (settings.ffb_periodic_restart && now - rumble_at >= kStatusInterval)) {
      if (changed && !SDL_UpdateHapticEffect(haptic, rumble_id, &effect)) {
        LogError("updating", 0);
        return;
      }
      rumble_last = effect;
      rumble_at = now;
      rumble_running = SDL_RunHapticEffect(haptic, rumble_id, 1);
    }
  }

  void Test(const Snapshot& snap) {
    const auto now = Clock::now();
    if (test_stage == 0) {
      std::optional<Settings> request;
      {
        std::lock_guard lock(mutex);
        request = std::exchange(test_request, std::nullopt);
        test_rumble = test_rumble_request;
      }
      if (!request) {
        return;
      }
      test_settings = *request;
      if (!OpenHaptic(ForceTarget(snap, test_settings))) {
        return;
      }
      if (!(features & (test_rumble ? SDL_HAPTIC_SINE : SDL_HAPTIC_CONSTANT))) {
        CloseHaptic();
        haptic_status = test_rumble ? "this device cannot vibrate" : "this device has no constant force";
        return;
      }
      if (test_rumble && RumbleMagnitude(65535, 65535, test_settings) <= 0) {
        CloseHaptic();
        haptic_status = "RUMBLE is at 0%";
        return;
      }
      test_stage = 1;
      test_next = now;
    }
    if (now < test_next) {
      return;
    }
    if (test_id >= 0) {
      SDL_DestroyHapticEffect(haptic, test_id);
      test_id = -1;
    }
    if (test_stage == 3) {
      CloseHaptic();
      haptic_status = test_rumble ? "tested: it should have buzzed, then rumbled"
                                  : "tested: it should have pulled LEFT, then RIGHT";
      test_stage = 0;
      return;
    }
    if (test_rumble) {
      // As the game's rumble plays (Rumble): the right (engine) motor buzzes, the left (road)
      // one rumbles, each at its share of RUMBLE.
      const bool engine = test_stage == 1;
      SDL_HapticEffect buzz{};
      buzz.type = SDL_HAPTIC_SINE;
      buzz.periodic.direction.type = SDL_HAPTIC_STEERING_AXIS;
      buzz.periodic.length = 800;
      buzz.periodic.period = engine ? 25 : 60;
      buzz.periodic.magnitude =
          engine ? RumbleMagnitude(0, 65535, test_settings) : RumbleMagnitude(65535, 0, test_settings);
      test_id = SDL_CreateHapticEffect(haptic, &buzz);
      if (test_id < 0 || !SDL_RunHapticEffect(haptic, test_id, 1)) {
        haptic_status = std::string("rumble test failed: ") + SDL_GetError();
        REXLOG_WARN("Wheel force feedback: {}", haptic_status);
        CloseHaptic();
        test_stage = 0;
        return;
      }
      haptic_status = engine ? "engine buzz" : "road rumble";
      ++test_stage;
      test_next = now + 900ms;
      return;
    }
    // A positive level pulls left: SDL, like DirectInput, names the side a force comes from.
    const int16_t level = TestForceLevel(test_settings);
    SDL_HapticEffect pulse{};
    pulse.type = SDL_HAPTIC_CONSTANT;
    pulse.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
    pulse.constant.length = 600;
    pulse.constant.level = test_stage == 1 ? level : int16_t(-level);
    test_id = SDL_CreateHapticEffect(haptic, &pulse);
    if (test_id < 0 || !SDL_RunHapticEffect(haptic, test_id, 1)) {
      haptic_status = std::string("test failed: ") + SDL_GetError();
      REXLOG_WARN("Wheel force feedback: {}", haptic_status);
      CloseHaptic();
      test_stage = 0;
      return;
    }
    haptic_status = test_stage == 1 ? "pulling LEFT" : "pulling RIGHT";
    ++test_stage;
    test_next = now + 650ms;
  }
};

DeviceMonitor::DeviceMonitor(Settings settings, bool launcher)
    : impl_(std::make_unique<Impl>(std::move(settings), launcher)) {}

DeviceMonitor::~DeviceMonitor() = default;

Snapshot DeviceMonitor::Read() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->published;
}

void DeviceMonitor::SetEffect(const ffb::Effect& effect) {
  if (effect.id < 1 || effect.id > kSlots) {
    return;
  }
  std::lock_guard lock(impl_->mutex);
  auto& slot = impl_->slots[effect.id];
  slot.effect = effect;
  slot.known = slot.dirty = true;
  ++slot.updates;
  impl_->heartbeat = Clock::now();
}

void DeviceMonitor::OperateEffect(uint32_t id, bool start) {
  if (id < 1 || id > kSlots) {
    return;
  }
  std::lock_guard lock(impl_->mutex);
  impl_->slots[id].run = start;
  impl_->heartbeat = Clock::now();
}

void DeviceMonitor::ClearEffects() {
  std::lock_guard lock(impl_->mutex);
  for (auto& slot : impl_->slots) {
    const uint32_t updates = slot.updates;
    slot = {};
    slot.updates = updates;
  }
  impl_->clear = true;
}

void DeviceMonitor::SetDriving(bool driving) {
  std::lock_guard lock(impl_->mutex);
  impl_->driving = driving;
}

void DeviceMonitor::SetRumble(uint16_t left, uint16_t right) {
  std::lock_guard lock(impl_->mutex);
  impl_->rumble_left = left;
  impl_->rumble_right = right;
}

void DeviceMonitor::TestForce(const Settings& settings, bool rumble) {
  std::lock_guard lock(impl_->mutex);
  impl_->test_request = settings;
  impl_->test_rumble_request = rumble;
}

}  // namespace fm4_static_recomp::wheel
