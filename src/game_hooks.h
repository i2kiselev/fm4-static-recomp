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

// Makes fm4_audio_pop_fix (SETTINGS → ADVANCED → AUDIO POP FIX in the launcher) switch the decoder's loop
// crossfade with the mix de-click; call once the command line is parsed.
void InstallAudioPopFix();
void InstallAudioCompatibility();

// Every guest file open, on the opening guest thread: the game's saving of a car
// thumbnail ends the readback hold its render began.
void NoteGuestFileOpen(std::string_view guest_path);

// True while the race render scenario is the last one pushed: the race itself, not the
// pre-race screen or the intro camera.
bool RaceViewActive();
// The race view is the start sequence (3, 2, 1), not yet the race after GO.
bool RaceCountdownActive();

// QUICK RESTART: asks the UI thread to run the pause menu's "restart confirmed" handler as soon
// as the pause menu is up; false cancels a request still waiting.
void RequestPauseMenuRestart(bool on);
bool PauseMenuRestartDone();
// Likewise START RACE on the pre-race screen as soon as it is up.
void RequestPreRaceStart(bool on);
// The pre-race screen has been up a while despite RequestPreRaceStart (A is pressed then).
bool PreRaceScreenStuck();
// Another race menu (not the pre-race screen) waiting on the player a while: a restart's loading went
// somewhere it cannot finish by itself.
bool RaceMenuWaiting();
// Any race menu scene up (none: the front-end menus).
bool RaceMenuSceneUp();
// The newest race menu screen is the pre-race screen (up, or left behind by its START RACE).
bool PreRaceScreenUp();

// QUICK RESTART's short start sequence (in the parsed 321Tuning/321Cams values); false puts the
// game's own back. False when they are not the disc's values (nothing changed).
bool SetQuickStartSequence(bool on);

// AUTO RESTART ON DIRTY LAP: the game has marked the player's lap dirty (off the track, into a
// wall or contact: the lap timer's warning), in a session asked for: `alone` with no AI cars on
// track (Hot Lap), `against_ai` in a race with them.
bool PlayerLapDirty(bool alone, bool against_ai);

}  // namespace fm4_static_recomp
