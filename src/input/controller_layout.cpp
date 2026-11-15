// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "input/controller_layout.h"

#include <array>
#include <atomic>

namespace fm4_static_recomp::input {
namespace {

enum Action : uint8_t {
  kNone,
  kGas,
  kBrake,
  kHandbrake,
  kShiftUp,
  kShiftDown,
  kClutch,
  kRewind,
  kLookStraightBack,
  kLookForward,
  kLookBack,
  kLookLeft,
  kLookRight,
  kHeatDamage,
  kTelemetry,
};

// The table's columns this layout sets; the left stick (steering), BACK and START are the
// same in every layout.
constexpr std::array<PadControl, 17> kColumns = {
    PadControl::kRStickUp,     PadControl::kRStickDown,    PadControl::kRStickLeft,
    PadControl::kRStickRight,  PadControl::kRStickPress,   PadControl::kDpadUp,
    PadControl::kDpadDown,     PadControl::kDpadLeft,      PadControl::kDpadRight,
    PadControl::kA,            PadControl::kB,             PadControl::kX,
    PadControl::kY,            PadControl::kLeftShoulder,  PadControl::kRightShoulder,
    PadControl::kLeftTrigger,  PadControl::kRightTrigger};

constexpr std::array<std::array<Action, kColumns.size()>, kControllerLayoutCount> kLayouts = {{
    {{kLookForward, kLookBack, kLookLeft, kLookRight, kHeatDamage, kTelemetry, kTelemetry, kNone, kNone, kHandbrake, kShiftUp, kShiftDown, kRewind, kClutch, kLookStraightBack, kBrake, kGas}},
    {{kLookForward, kLookBack, kLookLeft, kLookRight, kHeatDamage, kTelemetry, kTelemetry, kNone, kNone, kShiftDown, kHandbrake, kShiftUp, kRewind, kClutch, kLookStraightBack, kBrake, kGas}},
    {{kLookForward, kLookBack, kLookLeft, kLookRight, kHeatDamage, kTelemetry, kTelemetry, kNone, kNone, kShiftDown, kShiftUp, kHandbrake, kRewind, kClutch, kLookStraightBack, kBrake, kGas}},
    {{kLookForward, kLookBack, kLookLeft, kLookRight, kHeatDamage, kTelemetry, kTelemetry, kNone, kNone, kHandbrake, kRewind, kShiftDown, kShiftUp, kClutch, kLookStraightBack, kBrake, kGas}},
    {{kLookForward, kLookBack, kLookLeft, kLookRight, kHeatDamage, kTelemetry, kTelemetry, kNone, kNone, kHandbrake, kShiftDown, kRewind, kShiftUp, kClutch, kLookStraightBack, kBrake, kGas}},
    {{kLookForward, kLookBack, kLookLeft, kLookRight, kHeatDamage, kTelemetry, kTelemetry, kNone, kNone, kClutch, kHandbrake, kRewind, kLookStraightBack, kShiftDown, kShiftUp, kBrake, kGas}},
    {{kLookForward, kLookBack, kLookLeft, kLookRight, kHeatDamage, kTelemetry, kTelemetry, kNone, kNone, kLookStraightBack, kHandbrake, kRewind, kClutch, kShiftDown, kShiftUp, kBrake, kGas}},
    {{kLookForward, kLookBack, kLookLeft, kLookRight, kHeatDamage, kTelemetry, kTelemetry, kNone, kNone, kGas, kHandbrake, kBrake, kRewind, kClutch, kLookStraightBack, kShiftDown, kShiftUp}},
    {{kLookForward, kLookBack, kLookLeft, kLookRight, kHeatDamage, kTelemetry, kTelemetry, kNone, kNone, kGas, kBrake, kHandbrake, kRewind, kShiftDown, kShiftUp, kClutch, kLookStraightBack}},
    {{kClutch, kHandbrake, kNone, kNone, kHeatDamage, kTelemetry, kTelemetry, kNone, kNone, kLookBack, kLookRight, kLookLeft, kRewind, kShiftDown, kShiftUp, kBrake, kGas}},
    {{kShiftDown, kShiftUp, kNone, kNone, kHeatDamage, kTelemetry, kTelemetry, kNone, kNone, kLookBack, kLookRight, kLookLeft, kRewind, kClutch, kHandbrake, kBrake, kGas}},
    {{kGas, kBrake, kNone, kNone, kHeatDamage, kTelemetry, kTelemetry, kNone, kNone, kLookBack, kLookRight, kLookLeft, kRewind, kShiftDown, kShiftUp, kClutch, kHandbrake}},
    {{kLookForward, kLookBack, kLookLeft, kLookRight, kHeatDamage, kTelemetry, kTelemetry, kNone, kNone, kHandbrake, kShiftUp, kShiftDown, kRewind, kLookStraightBack, kClutch, kGas, kBrake}},
}};

std::atomic<uint32_t> g_layout{0};
std::atomic<bool> g_dpad_steering{false};

std::string_view Label(Action action, PadControl control) {
  switch (action) {
    case kNone:
      return {};
    case kGas:
      return "Throttle";
    case kBrake:
      return "Brake";
    case kHandbrake:
      return "Handbrake";
    case kShiftUp:
      return "Gear up";
    case kShiftDown:
      return "Gear down";
    case kClutch:
      return "Clutch";
    case kRewind:
      return "Rewind";
    case kLookStraightBack:
      return "Look straight back";
    case kLookForward:
      return "Look forward";
    case kLookBack:
      return "Look back";
    case kLookLeft:
      return "Look left";
    case kLookRight:
      return "Look right";
    case kHeatDamage:
      return "Heat and damage";
    case kTelemetry:
      return control == PadControl::kDpadDown ? "Telemetry down" : "Telemetry up";
  }
  return {};
}

}  // namespace

void SetControllerLayout(uint32_t layout) { g_layout.store(layout, std::memory_order_relaxed); }

uint32_t ControllerLayout() { return g_layout.load(std::memory_order_relaxed); }

void SetDpadSteering(bool on) { g_dpad_steering.store(on, std::memory_order_relaxed); }

bool DpadSteering() { return g_dpad_steering.load(std::memory_order_relaxed); }

std::string_view DrivingAction(uint32_t layout, PadControl control) {
  switch (control) {
    case PadControl::kLStickLeft:
      return "Steer left";
    case PadControl::kLStickRight:
      return "Steer right";
    case PadControl::kBack:
      return "Camera";
    case PadControl::kStart:
      return "Pause";
    default:
      break;
  }
  const auto& actions = kLayouts[layout < kControllerLayoutCount ? layout : 0];
  for (size_t i = 0; i < kColumns.size(); ++i) {
    if (kColumns[i] == control) {
      return Label(actions[i], control);
    }
  }
  return {};
}

}  // namespace fm4_static_recomp::input
