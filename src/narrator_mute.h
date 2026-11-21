// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace fm4_static_recomp {

// Silences every clip of a World Tour narrator bank (Media/audio/VO/<language>/vo.fsb, FSB4)
// without changing its size: each sample's default volume goes to 0 and its length to one
// 512-sample XMA frame, because the sound events set their own volume. False, with the bank
// unchanged, when it is not laid out like the disc's (a modded or already muted bank).
bool MuteNarratorBank(std::span<uint8_t> bank);

// Registers the muted bank of every language for the guest when fm4_mute_narrator is set.
void InstallNarratorMute(const std::filesystem::path& game_data_root);

// Mutes or restores the bank the game has already loaded (it reads it once, as it starts), in the
// guest's memory: the next clip the narrator plays takes it. The search runs on its own thread,
// which takes only the newest choice; the bank served at the next start follows it too.
void RequestNarratorMute(const std::filesystem::path& game_data_root, bool mute);
// Ends that thread: before the runtime goes away.
void StopNarratorMute();

}  // namespace fm4_static_recomp
