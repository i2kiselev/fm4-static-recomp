// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <memory>

#include "ui/hostui/menu.h"

namespace fm4_static_recomp::ui {

// PC OPTIONS > SIGNAGE (src/track_signage.h): TRACK SIGNAGE on or off, the tracks, and each track's
// boards with the picture each one shows (any PNG of the signage folder, or the game's), previewed
// in the description panel. Changes are drafted until SAVE & APPLY, which saves signage.toml and
// shows them on track at once.
std::unique_ptr<hostui::MenuScreen> CreateSignagePage();

}  // namespace fm4_static_recomp::ui
