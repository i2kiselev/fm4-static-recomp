// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "track_signage.h"

namespace fm4_static_recomp::signage {

// The running side of track signage (src/track_signage.h): the signage folder's pictures, the
// game's own boards (decoded from the track archives, for the SIGNAGE page's previews) and the
// choices, applied live from a thread that looks at the folder every second.

// A PNG in the signage folder.
struct Picture {
  std::string file;  // its name in the folder
  uint32_t width = 0;
  uint32_t height = 0;
  std::shared_ptr<const Image> thumbnail;  // at most 512 x 256
};

// Reads the choices, makes the folder and starts the thread. `game_root` holds Media\Tracks.
void Start(const std::filesystem::path& folder, const std::filesystem::path& game_root,
           const std::filesystem::path& choices_file);
std::filesystem::path Folder();
// Opens the folder in the system's file browser (Explorer on Windows).
bool OpenFolder();

// The folder's pictures, by name (as last read: the thread rereads the folder every second).
std::vector<Picture> Pictures();
// The game's board as a preview (at most 512 x 256), or nullptr until the thread has decoded it.
std::shared_ptr<const Image> BoardPreview(const Track& track, const Board& board);

// The choices in use, and their file.
Choices CurrentChoices();
// TRACK SIGNAGE as signage.toml has it, whatever a launch option says this session.
bool SavedEnabled();
// Ends the worker: before the runtime goes away.
void Stop();
// Where SaveChoices writes, without starting the worker (the page's tests).
void SetChoicesFile(const std::filesystem::path& file);
// Saves the choices and TRACK SIGNAGE on or off (signage.toml, written whole) and shows them;
// false when it couldn't save.
bool SaveChoices(const Choices& choices, bool enabled);
// The boards showing a picture now.
size_t ShownCount();
// Whether a picture of the player's own is on this texture now (track_textures leaves it alone then).
bool ShowsPicture(uint64_t hash);

}  // namespace fm4_static_recomp::signage
