// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
// The racing wheel as the game sees it: one Xbox 360 wheel (XInput subtype 2) on player one,
// built from bindings on any number of USB devices (a wheel base, separate pedals, a shifter,
// a button box), playing the game's own force feedback on the force device. The binding syntax
// is the SDK's (docs/RACING_WHEELS.md).
#include "wheel/wheel_input.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/input/xinputd_ffb.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xtypes.h>

#include "wheel/wheel_devices.h"
#include "wheel/wheel_settings.h"

namespace fm4_static_recomp::wheel {

namespace {

using namespace rex::input;
using rex::X_RESULT;
using rex::X_STATUS;

constexpr DeviceId kWheelId = static_cast<DeviceId>(0xF400000000000001ull);

// The driver's monitor, for the force feedback hooks on the game's threads.
std::mutex service_mutex;
std::weak_ptr<DeviceMonitor> service;
std::atomic<bool> wheel_connected{false};

std::shared_ptr<DeviceMonitor> Service() {
  std::lock_guard lock(service_mutex);
  return service.lock();
}

// Player one is the wheel while one is connected; the other users keep the SDK's slots, and
// the keyboard-and-mouse emulation stays on player one.
class WheelAssignment final : public DeviceAssignment {
 public:
  void OnDevicesChanged(const std::vector<DeviceInfo>& devices) override {
    wheel_ = std::any_of(devices.begin(), devices.end(),
                         [](const DeviceInfo& d) { return d.id == kWheelId; });
    std::vector<DeviceInfo> others;
    std::copy_if(devices.begin(), devices.end(), std::back_inserter(others),
                 [](const DeviceInfo& d) { return d.id != kWheelId; });
    slots_.OnDevicesChanged(others);
  }
  void DevicesForUser(uint32_t user, std::vector<DeviceId>& out) const override {
    out.clear();
    if (!wheel_) {
      slots_.DevicesForUser(user, out);
      return;
    }
    // The wheel's capabilities stay player one's, whatever else is plugged in.
    if (user == 0) {
      out.push_back(kWheelId);
    }
  }

 private:
  bool wheel_ = false;
  SlotAssignment slots_;
};

// The game sends the wheel's rumble (engine, kerbs) through the kernel's XInputdFFSetRumble, which
// the SDK answers and hands to its registered sink; the effects themselves come through the hooks
// (wheel_hooks.cpp), so the sink takes only the rumble.
class RumbleSink final : public xinputd_ffb::Sink {
 public:
  explicit RumbleSink(std::shared_ptr<DeviceMonitor> monitor) : monitor_(std::move(monitor)) {}
  bool HasForceFeedback() override { return true; }
  void SetEffect(const xinputd_ffb::Effect&) override {}
  void UpdateEffect(const xinputd_ffb::Effect&, uint32_t) override {}
  void EffectOperation(uint32_t, uint32_t) override {}
  void DeviceControl(uint32_t) override {}
  void SetDeviceGain(uint32_t) override {}
  void SetRumble(uint16_t left, uint16_t right) override { monitor_->SetRumble(left, right); }

 private:
  std::shared_ptr<DeviceMonitor> monitor_;
};

class WheelDriver final : public InputDriver {
 public:
  explicit WheelDriver(Settings settings)
      : InputDriver(nullptr, 0),
        composer_(settings),
        monitor_(std::make_shared<DeviceMonitor>(std::move(settings), false)) {
    std::lock_guard lock(service_mutex);
    service = monitor_;
    xinputd_ffb::SetSink(&rumble_sink_);
  }
  ~WheelDriver() override {
    // Waits for calls in flight, so the sink can go with the driver.
    if (xinputd_ffb::GetSink() == &rumble_sink_) {
      xinputd_ffb::SetSink(nullptr);
    }
    wheel_connected = false;
    std::lock_guard lock(service_mutex);
    service.reset();
  }

  X_STATUS Setup() override { return X_STATUS_SUCCESS; }

  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    const Snapshot s = monitor_->Read();
    const Device* base = composer_.Base(s);
    if (s.generation != generation_) {
      generation_ = s.generation;
      for (const Device& d : s.devices) {
        REXLOG_INFO("Wheel device: \"{}\" {:04x}:{:04x} ({} axes, {} buttons, {} hats){}{}",
                    d.name, d.vendor, d.product, d.axes.size(), d.buttons.size(), d.hats.size(),
                    d.wheel ? ", SDL wheel" : "", base && base->id == d.id ? ", the wheel base" : "");
      }
      if (s.ready && !base) {
        REXLOG_WARN("Wheel base \"{}\" is not connected", composer_.settings().device);
      }
    }
    if (s.haptic_status != haptic_status_) {
      haptic_status_ = s.haptic_status;
      REXLOG_INFO("Wheel force feedback: {} (\"{}\", mode {}, strength {}%)", s.haptic_status,
                  s.ffb_name, composer_.settings().ffb_mode, composer_.settings().ffb_strength);
    }
    if (composer_.settings().log_inputs) {
      LogInputs(s);
    }
    wheel_connected = base != nullptr;
    if (!base) {
      return;
    }
    DeviceInfo info;
    info.id = kWheelId;
    info.name = base->name;
    info.guid = "fm4-wheel";
    info.vendor_id = base->vendor;
    info.product_id = base->product;
    out.push_back(std::move(info));
  }

  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out) override {
    if (id != kWheelId || !out) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
    const Snapshot s = monitor_->Read();
    if (!composer_.Base(s)) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
    *out = {};
    out->packet_number = ++packet_;
    out->gamepad = composer_.Compose(s);
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t, X_INPUT_CAPABILITIES* out) override {
    if (id != kWheelId || !out || !composer_.Base(monitor_->Read())) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
    *out = {};
    out->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
    out->sub_type = 0x02;  // XINPUT_DEVSUBTYPE_WHEEL
    // The game takes its wheel path only for a wheel that reports force feedback; with none,
    // its effects are dropped.
    out->flags = X_INPUT_CAPS_FFB_SUPPORTED;
    out->gamepad.buttons = 0xF3FF;
    out->gamepad.left_trigger = out->gamepad.right_trigger = 0xFF;
    out->gamepad.thumb_lx = int16_t(0xFFFF);
    out->gamepad.thumb_ly = int16_t(0xFFFF);
    // A thumb_rx range tells Forza's wheel code that an H-shifter is there.
    out->gamepad.thumb_rx = composer_.shifter() ? int16_t(0xFFFF) : int16_t(0);
    out->gamepad.thumb_ry = int16_t(0xFFFF);
    out->vibration.left_motor_speed = out->vibration.right_motor_speed = 0xFFFF;
    return X_ERROR_SUCCESS;
  }

  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override {
    if (id != kWheelId || !vibration) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
    monitor_->SetRumble(vibration->left_motor_speed, vibration->right_motor_speed);
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t, X_INPUT_KEYSTROKE*) override {
    return id == kWheelId ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
  }

 private:
  void LogInputs(const Snapshot& s) {
    for (const Device& d : s.devices) {
      Device& last = logged_[d.name + "|" + d.path];
      if (last.axes.size() != d.axes.size() || last.buttons.size() != d.buttons.size() ||
          last.hats.size() != d.hats.size()) {
        last = d;
        continue;
      }
      for (size_t i = 0; i < d.axes.size(); ++i) {
        if (std::abs(d.axes[i] - last.axes[i]) > 4000) {
          REXLOG_INFO("Wheel input: \"{}\" axis{} {}", d.name, i, d.axes[i]);
          last.axes[i] = d.axes[i];
        }
      }
      for (size_t i = 0; i < d.buttons.size(); ++i) {
        if (d.buttons[i] != last.buttons[i]) {
          REXLOG_INFO("Wheel input: \"{}\" button {} {}", d.name, i,
                      d.buttons[i] ? "down" : "up");
        }
      }
      for (size_t i = 0; i < d.hats.size(); ++i) {
        if (d.hats[i] != last.hats[i]) {
          REXLOG_INFO("Wheel input: \"{}\" hat {} = {}", d.name, i, d.hats[i]);
        }
      }
      last.buttons = d.buttons;
      last.hats = d.hats;
    }
  }

  Composer composer_;
  std::shared_ptr<DeviceMonitor> monitor_;
  RumbleSink rumble_sink_{monitor_};
  uint64_t generation_ = ~0ull;
  uint32_t packet_ = 0;
  std::string haptic_status_;
  std::map<std::string, Device> logged_;
};

}  // namespace

void ConfigureWheelInput(rex::RuntimeConfig& config) {
  auto previous = std::move(config.input_factory);
  config.input_factory = [previous = std::move(previous)](bool tool_mode)
      -> std::unique_ptr<rex::system::IInputSystem> {
    auto input = previous ? previous(tool_mode) : CreateDefaultInputSystem(tool_mode);
    const Settings s = ReadSettings();
    auto* system = dynamic_cast<InputSystem*>(input.get());
    if (tool_mode || !s.enabled || !system) {
      return input;
    }
    REXLOG_INFO("Wheel input: base \"{}\" steering axis {} ({}%), throttle \"{}\", brake \"{}\", "
                "clutch \"{}\", buttons \"{}\"; force feedback {} at {}% on \"{}\", restarted every "
                "second: {}",
                s.device, s.steering_axis, SteeringScale(s), s.throttle, s.brake, s.clutch,
                s.buttons, s.ffb_mode, s.ffb_strength,
                s.ffb_device.empty() ? s.device : s.ffb_device, s.ffb_periodic_restart);
    // The SDK's wheel driver would take the same wheel and force device.
    rex::cvar::SetFlagByName("wheel_enabled", "false");
    system->AddDriver(std::make_unique<WheelDriver>(s));
    system->SetDeviceAssignment(std::make_unique<WheelAssignment>());
    return input;
  };
}

bool HandlesForceFeedback(uint32_t user) {
  return user == 0 && wheel_connected.load(std::memory_order_relaxed) && Service() != nullptr;
}

void SubmitEffect(const rex::input::xinputd_ffb::Effect& effect) {
  static std::atomic<uint32_t> seen{0};
  if (effect.id >= 1 && effect.id <= 5 &&
      !(seen.fetch_or(1u << effect.id) & (1u << effect.id))) {
    REXLOG_INFO("Wheel effect: {}", rex::input::xinputd_ffb::DescribeEffect(effect));
  }
  if (auto s = Service()) {
    s->SetEffect(effect);
  }
}

void OperateEffect(uint32_t id, bool start) {
  if (auto s = Service()) {
    s->OperateEffect(id, start);
  }
}

void ResetEffects() {
  if (auto s = Service()) {
    s->ClearEffects();
  }
}

void SetDriving(bool driving) {
  if (auto s = Service()) {
    s->SetDriving(driving);
  }
}

}  // namespace fm4_static_recomp::wheel
