// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>

#include <rex/input/xinputd_ffb.h>

namespace rex {
struct RuntimeConfig;
}

namespace fm4_static_recomp::wheel {

// With fm4_wheel_enabled, wraps config.input_factory so that player one is the racing wheel,
// built from the bindings on any number of USB devices, and turns the SDK's own wheel driver
// off. Reads the settings when the runtime creates its input system.
void ConfigureWheelInput(rex::RuntimeConfig& config);

// The game's native force feedback (src/wheel/wheel_hooks.cpp). True when the wheel driver
// answers for this guest user: player one, while a wheel is connected.
bool HandlesForceFeedback(uint32_t user);
void SubmitEffect(const rex::input::xinputd_ffb::Effect& effect);
void OperateEffect(uint32_t id, bool start);
void ResetEffects();
void SetDriving(bool driving);

}  // namespace fm4_static_recomp::wheel
