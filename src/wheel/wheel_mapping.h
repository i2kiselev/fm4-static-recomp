// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once
// The racing wheel as the game reads it, shared by the game's driver and the launcher's live
// view (--fm4_wheel_probe), so the WHEEL panel shows exactly what the game will receive. The
// binding syntax and the steering, pedal and effect maths are the SDK's
// (rex/input/sdl/wheel_mapping.h), from Pinyon Shift's racing wheel work
// (https://github.com/arcanite24/pinyon-shift, BSD 3-Clause; see THIRD_PARTY_NOTICES.md); this
// adds FM4's scales and a wheel made of several USB devices. Nothing here touches SDL.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rex/input/input.h>
#include <rex/input/sdl/wheel_mapping.h>
#include <rex/input/xinputd_ffb.h>

#include "wheel/wheel_settings.h"

namespace fm4_static_recomp::wheel {

// One connected controller as SDL reports it, opened as a raw joystick (wheel bases, pedals,
// shifters and button boxes alike, without a gamepad mapping).
struct Device {
  uint32_t id = 0;  // SDL instance id; never stored
  std::string name, path;
  uint16_t vendor = 0, product = 0;
  bool wheel = false;  // SDL types it as a wheel
  std::vector<int16_t> axes, rest;  // rest: the axes when the device opened
  std::vector<bool> buttons;
  std::vector<uint8_t> hats;
};

struct Snapshot {
  std::vector<Device> devices;
  uint64_t generation = 0;  // changes when a device connects or disconnects
  bool ready = false;  // the first device list is in
  std::string ffb_name, haptic_status;
  bool force_supported = false;  // the force device plays constant force
  bool rumble_supported = false;
  bool test_busy = false;
};

const Device* FindDevice(const std::vector<Device>& devices, std::string_view match);
// The text that names exactly this device among those connected (what the launcher stores):
// "vvvv:pppp", else its name, else "path:<path>".
std::string DeviceMatch(const Device& device, const std::vector<Device>& all);

// Steering sensitivity in percent: the sensitivity, times the wheel's rotation over the game's
// when game_rotation is set (900 on 360: 250 %).
int SteeringScale(const Settings& s);
// Pad buttons for "B", "A+LB" or "A,LB"; 0 for none.
uint16_t ButtonsFromNames(std::string_view names);

// What the game reads from the bindings.
class Composer {
 public:
  explicit Composer(Settings settings);
  const Settings& settings() const { return s_; }
  bool shifter() const { return shifter_; }
  // The wheel base: s.device, else the first device SDL types as a wheel, else the first of a
  // known wheel vendor.
  const Device* Base(const Snapshot& snap) const;
  rex::input::X_INPUT_GAMEPAD Compose(const Snapshot& snap) const;
  // 0..255 for throttle (0), brake (1), clutch (2); 0 when unbound or missing.
  uint8_t Pedal(const Snapshot& snap, int which) const;

 private:
  const Device* On(const Snapshot& snap, const std::string& device) const;
  Settings s_;
  std::optional<rex::input::sdl::WheelAxisBinding> pedals_[3];
  std::vector<rex::input::sdl::WheelButtonBinding> buttons_;
  uint16_t clutch_buttons_ = 0;
  bool shifter_ = false;
};

enum class FfbMode : uint8_t { kOff, kEffects, kRumble };
FfbMode ParseFfbMode(std::string_view text);

// FM4's native wheel effects (XInput-FF descriptors, the SDK's layout) quantise forces and
// coefficients to +/-127 and gains and saturations to 255.
constexpr int kEffectMagnitudeMax = 127;
constexpr int kEffectSaturationMax = 255;

// SDL constant-force level: magnitude x gain, lifted to ffb_min_force, times ffb_strength (up
// to 200 %, clipped at full scale), reversed by ffb_invert.
int16_t ConstantLevel(const rex::input::xinputd_ffb::Effect& effect, const Settings& s);
// SDL condition values (spring, damper, inertia, friction): gain, ffb_strength and that
// effect's own share scale the coefficients.
rex::input::sdl::WheelCondition ConditionValues(const rex::input::xinputd_ffb::Effect& effect,
                                                const Settings& s);
// The Test's constant force: ffb_strength, 100 % being half the base's maximum.
int16_t TestForceLevel(const Settings& s);
// The game's rumble as one vibration, 0..32767: the stronger motor after the engine (right)
// and road (left) shares.
int16_t RumbleMagnitude(uint16_t left, uint16_t right, const Settings& s);

}  // namespace fm4_static_recomp::wheel
