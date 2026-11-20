// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <functional>
#include <optional>
#include <string>

namespace rex::ui {
class Window;
}

namespace fm4_static_recomp::ui {

// The system's folder chooser over `window`, starting at `current` (when set). `done` gets the
// chosen folder (UTF-8), or nullopt when cancelled or failed, on whichever thread the system
// answers. Call on the UI thread.
void ShowFolderDialog(rex::ui::Window& window, const std::string& current,
                      std::function<void(std::optional<std::string>)> done);

}  // namespace fm4_static_recomp::ui
