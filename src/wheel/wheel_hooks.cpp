// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
// FM4's own wheel force feedback (config/rexglue/analysis/main-xex.toml, "Racing wheel").
//
// The game drives a force feedback wheel through six small XInput-FF wrappers: each turns the
// user index into the wheel's handle and calls the XInputdFF* function it resolved at start.
// While the racing wheel driver has player one (src/wheel/wheel_input.cpp), these hooks answer
// the wrappers at their entry: the five effect slots go to the force device, decoded from the
// game's 116-byte descriptors, and each call completes at once. Otherwise the game's own code
// runs. The per-frame wheel update tells whether the player is driving, so the forces stop in
// menus and pauses. Found in the 1.0 code image by the wrappers' shape (each calls a resolved
// XInputdFF* pointer) and the update's object.
#include <atomic>
#include <cstdint>

#include <rex/input/xinputd_ffb.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <rex/logging.h>

#include "wheel/wheel_input.h"

namespace {

namespace ffb = rex::input::xinputd_ffb;
namespace wheel = fm4_static_recomp::wheel;

constexpr uint32_t kSlots = 5;
constexpr uint64_t kInvalidParameter = 87;  // ERROR_INVALID_PARAMETER

uint8_t* Membase() {
  return rex::system::kernel_state()->memory()->virtual_membase();
}

bool Buffer(uint32_t address, uint32_t length) {
  return address >= 0x10000 && !(address & 3) && address <= 0xFFFFFFFFu - length;
}

uint32_t Load32(uint32_t address) {
  const uint8_t* p = Membase() + address;
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void Store32(uint32_t address, uint32_t value) {
  uint8_t* p = Membase() + address;
  p[0] = uint8_t(value >> 24);
  p[1] = uint8_t(value >> 16);
  p[2] = uint8_t(value >> 8);
  p[3] = uint8_t(value);
}

// The call is done: its XOVERLAPPED says so and the wrapper returns success.
void Complete(PPCRegister& r3, uint32_t overlapped) {
  if (overlapped && Buffer(overlapped, 28)) {
    rex::system::kernel_state()->CompleteOverlappedImmediate(overlapped, 0);
  }
  r3.u64 = 0;
}

}  // namespace

// (user, info): the device info. Five effect slots, as the game asks for.
bool Fm4WheelFfbDeviceInfo(PPCRegister& r3, PPCRegister& r4) {
  if (!wheel::HandlesForceFeedback(r3.u32)) {
    return false;
  }
  if (!Buffer(r4.u32, 8)) {
    r3.u64 = kInvalidParameter;
    return true;
  }
  Store32(r4.u32, 0);
  Store32(r4.u32 + 4, kSlots);
  r3.u64 = 0;
  return true;
}

// (user, descriptors, count, overlapped): downloads effects.
bool Fm4WheelFfbSetEffect(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r6) {
  if (!wheel::HandlesForceFeedback(r3.u32)) {
    return false;
  }
  const uint32_t count = r5.u32;
  if (!count || count > kSlots || !Buffer(r4.u32, count * ffb::kDescriptorSize)) {
    r3.u64 = kInvalidParameter;
    return true;
  }
  for (uint32_t i = 0; i < count; ++i) {
    wheel::SubmitEffect(ffb::DecodeEffect(Membase() + r4.u32 + i * ffb::kDescriptorSize));
  }
  Complete(r3, r6.u32);
  return true;
}

// (user, descriptor, flags, overlapped): updates one effect, every frame while driving.
bool Fm4WheelFfbUpdateEffect(PPCRegister& r3, PPCRegister& r4, PPCRegister& r6) {
  if (!wheel::HandlesForceFeedback(r3.u32)) {
    return false;
  }
  if (!Buffer(r4.u32, ffb::kDescriptorSize)) {
    r3.u64 = kInvalidParameter;
    return true;
  }
  wheel::SubmitEffect(ffb::DecodeEffect(Membase() + r4.u32));
  Complete(r3, r6.u32);
  return true;
}

// (user, operations, count, ?, overlapped): {effect id, operation} pairs; 1 and 2 start an
// effect, 3 stops it.
bool Fm4WheelFfbEffectOperation(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5,
                                PPCRegister& r7) {
  if (!wheel::HandlesForceFeedback(r3.u32)) {
    return false;
  }
  const uint32_t count = r5.u32;
  if (!count || count > kSlots || !Buffer(r4.u32, count * 8)) {
    r3.u64 = kInvalidParameter;
    return true;
  }
  static std::atomic<int> logged{0};
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t id = Load32(r4.u32 + i * 8), operation = Load32(r4.u32 + i * 8 + 4);
    if (logged.fetch_add(1) < 40) {
      REXLOG_INFO("Wheel effect operation: effect {} op {}", id, operation);
    }
    wheel::OperateEffect(id, operation == 1 || operation == 2);
  }
  Complete(r3, r7.u32);
  return true;
}

// (user, overlapped) and (user, command, overlapped): the device resets; every effect stops.
bool Fm4WheelFfbReset(PPCRegister& r3, PPCRegister& r4) {
  if (!wheel::HandlesForceFeedback(r3.u32)) {
    return false;
  }
  REXLOG_INFO("Wheel force feedback: the game stops every effect");
  wheel::ResetEffects();
  Complete(r3, r4.u32);
  return true;
}

bool Fm4WheelFfbDeviceControl(PPCRegister& r3, PPCRegister& r5) {
  if (!wheel::HandlesForceFeedback(r3.u32)) {
    return false;
  }
  REXLOG_INFO("Wheel force feedback: the game resets the device");
  wheel::ResetEffects();
  Complete(r3, r5.u32);
  return true;
}

// The wheel's per-frame update (this): user index at +0, the driving flag at +5.
void Fm4WheelUpdate(PPCRegister& r3) {
  const uint32_t object = r3.u32;
  if (!Buffer(object, 8) || !wheel::HandlesForceFeedback(Load32(object))) {
    return;
  }
  const bool driving = Membase()[object + 5] != 0;
  static std::atomic<int> last{-1};
  if (last.exchange(driving) != int(driving)) {
    REXLOG_INFO("Wheel update: driving {}", driving);
  }
  wheel::SetDriving(driving);
}
