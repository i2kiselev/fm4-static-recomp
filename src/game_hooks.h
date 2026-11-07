// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace fm4_static_recomp {

// Runs on a guest thread when the player picks GRAPHICS in My Profile ->
// Options.
void SetGraphicsPageOpener(std::function<void()> opener);

// Runs on the game's main loop thread once a tick, after the race flag and the
// input context are current.
void SetInputTick(std::function<void()> tick);

// Rewrites the game's strings that name pad buttons (input/prompt_strings.h):
// the source gets the string's key and the game's text, and answers the text to
// show or nullopt. Runs on guest threads.
void SetPromptTextSource(
    std::function<std::optional<std::u16string>(uint16_t key, std::u16string_view text)> source);

// Makes fm4_road_detail_blur, fm4_car_dirt and fm4_upscaler_replay_blur (with whether a
// temporal upscaler runs) drive the GPU settings, now and on each change; call once the GPU
// plugin has registered them.
void InstallPixelShaderSwitches();

// Makes fm4_audio_pop_fix (AUDIO POP FIX in the launcher's SETTINGS) switch the decoder's loop
// crossfade with the mix de-click; call once the command line is parsed.
void InstallAudioPopFix();

// Every guest file open, on the opening guest thread: the game's saving of a car
// thumbnail ends the readback hold its render began.
void NoteGuestFileOpen(std::string_view guest_path);


}  // namespace fm4_static_recomp
