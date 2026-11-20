// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "ui/folder_dialog.h"

#include <filesystem>
#include <mutex>

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/window.h>

namespace fm4_static_recomp::ui {
namespace {

using Done = std::function<void(std::optional<std::string>)>;

// The open chooser (one at a time). It lives for the whole run: SDL's Windows chooser can answer
// twice (an error when the modern dialog cannot open, then the answer of the older dialog it falls
// back to), so the answer is matched here, not through memory it would free.
struct Request {
  std::mutex mutex;
  Done done;
  bool open = false;
  bool failed = false;  // SDL reported an error; a fallback dialog may still answer
} g_request;

// The SDL window behind `window` (matched by its native handle where there is one).
SDL_Window* SdlWindowOf(rex::ui::Window& window) {
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  if (!windows) return nullptr;
  SDL_Window* found = count > 0 ? windows[0] : nullptr;
#if defined(_WIN32)
  const void* hwnd = window.GetNativeWindowHandle();
  for (int i = 0; hwnd && i < count; ++i) {
    if (SDL_GetPointerProperty(SDL_GetWindowProperties(windows[i]), SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                               nullptr) == hwnd) {
      found = windows[i];
      break;
    }
  }
#else
  (void)window;
#endif
  SDL_free(windows);
  return found;
}

void SDLCALL OnChosen(void*, const char* const* files, int) {
  Done done;
  {
    std::lock_guard lock(g_request.mutex);
    if (!g_request.open) return;
    if (!files) {
      // Not the end on Windows: the older dialog may follow. A new request may replace this one.
      g_request.failed = true;
      REXLOG_WARN("Folder chooser: {}", SDL_GetError());
      return;
    }
    done = std::move(g_request.done);
    g_request.done = nullptr;
    g_request.open = false;
    g_request.failed = false;
  }
  done(files[0] ? std::optional<std::string>(files[0]) : std::nullopt);
}

}  // namespace

void ShowFolderDialog(rex::ui::Window& window, const std::string& current, Done done) {
  {
    std::lock_guard lock(g_request.mutex);
    if (g_request.open && !g_request.failed) return;  // already open
    g_request.done = std::move(done);
    g_request.open = true;
    g_request.failed = false;
  }
  // The chooser opens in the folder holding `current`, with `current` selected; Windows needs
  // its own separators for that.
  std::string start;
  std::error_code ec;
  if (!current.empty() && std::filesystem::is_directory(rex::to_path(current), ec)) {
    start = rex::path_to_utf8(rex::to_path(current).make_preferred());
  }
  SDL_ShowOpenFolderDialog(OnChosen, nullptr, SdlWindowOf(window), start.empty() ? nullptr : start.c_str(),
                           false);
}

}  // namespace fm4_static_recomp::ui
