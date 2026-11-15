// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <memory>

#include <rex/input/xinputd_ffb.h>

#include "wheel/wheel_mapping.h"

namespace fm4_static_recomp::wheel {

// The connected controllers and the force device, on a worker thread that makes every SDL call
// (SDL haptics are not thread-safe). This SDL instance is the game executable's own, apart from
// the runtime's: it opens joysticks raw and plays force feedback, and leaves raw input to the
// runtime's gamepads.
class DeviceMonitor {
 public:
  // In launcher mode (--fm4_wheel_probe) the force device is opened only for TestForce, so the
  // game can have it.
  DeviceMonitor(Settings settings, bool launcher);
  ~DeviceMonitor();
  DeviceMonitor(const DeviceMonitor&) = delete;
  DeviceMonitor& operator=(const DeviceMonitor&) = delete;

  Snapshot Read() const;

  // The game's five effect slots (XInput-FF), as it downloads and updates them.
  void SetEffect(const rex::input::xinputd_ffb::Effect& effect);
  void OperateEffect(uint32_t id, bool start);
  void ClearEffects();
  // The game's wheel update says whether the player is driving; effects play only then.
  void SetDriving(bool driving);
  void SetRumble(uint16_t left, uint16_t right);
  // Pulls left, then right, at TestForceLevel; the strength, direction and device are the ones
  // given (the launcher's unsaved values).
  // With `rumble`, plays the rumble instead: the engine's buzz, then the road's.
  void TestForce(const Settings& settings, bool rumble = false);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fm4_static_recomp::wheel
