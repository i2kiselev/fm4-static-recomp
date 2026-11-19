// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
// The signage folder's watcher (apart from track_signage.cpp, which the unit tests build without
// the SDK's image decoder).
#include "track_signage.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <thread>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/image_decode.h>

REXCVAR_DECLARE(bool, fm4_track_signage);

namespace fm4_static_recomp::signage {

namespace {

using rex::graphics::texture_replacement::SetRuntimeReplacement;

char Lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }

std::optional<std::vector<uint8_t>> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

void Start(const std::filesystem::path& folder) {
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  {
    const auto readme = folder / "README.txt";
    const std::string text = Readme();
    const auto old = ReadFile(readme);
    if (!old || std::string(old->begin(), old->end()) != text) {
      std::ofstream(readme, std::ios::binary | std::ios::trunc) << text;
    }
  }
  std::thread([folder] {
    struct Shown {
      std::filesystem::file_time_type stamp;
      uintmax_t size = 0;
    };
    std::map<uint64_t, Shown> shown;  // board hash -> the file shown on it
    std::map<std::string, bool> warned;
    for (;;) {
      std::map<uint64_t, std::pair<const Board*, std::filesystem::path>> present;
      if (REXCVAR_GET(fm4_track_signage)) {
        std::error_code it_ec;
        for (std::filesystem::directory_iterator it(folder, it_ec), end; !it_ec && it != end; it.increment(it_ec)) {
          std::error_code item;
          const auto& path = it->path();
          std::string extension = rex::path_to_utf8(path.extension());
          std::transform(extension.begin(), extension.end(), extension.begin(), Lower);
          if (!it->is_regular_file(item) || extension != ".png") continue;
          const std::string stem = rex::path_to_utf8(path.stem());
          const Board* board = FindBoard(stem);
          if (!board) {
            if (!warned[stem]) REXLOG_WARN("Track signage: {}.png names no board (see README.txt)", stem);
            warned[stem] = true;
            continue;
          }
          present[board->hash] = {board, path};
        }
      }
      for (const auto& [hash, entry] : present) {
        const auto& [board, path] = entry;
        std::error_code stat_ec;
        const auto stamp = std::filesystem::last_write_time(path, stat_ec);
        const auto size = std::filesystem::file_size(path, stat_ec);
        if (stat_ec) continue;
        const auto found = shown.find(hash);
        if (found != shown.end() && found->second.stamp == stamp && found->second.size == size) continue;
        const auto bytes = ReadFile(path);
        int width = 0, height = 0;
        const std::vector<uint8_t> rgba =
            bytes ? rex::ui::DecodeImageRGBA(bytes->data(), bytes->size(), width, height) : std::vector<uint8_t>{};
        shown[hash] = {stamp, size};  // also a broken file: not read again until it changes
        if (rgba.empty() || width <= 0 || height <= 0) {
          REXLOG_WARN("Track signage: {} is not a PNG it can read", rex::path_to_utf8(path.filename()));
          continue;
        }
        const auto image = BuildImage(*board, rgba, uint32_t(width), uint32_t(height));
        SetRuntimeReplacement(hash, image);
        REXLOG_INFO("Track signage: {} on {} ({} x {}, {} levels)", rex::path_to_utf8(path.filename()),
                    board->what, image->width, image->height, image->levels);
      }
      for (auto it = shown.begin(); it != shown.end();) {
        if (present.contains(it->first)) {
          ++it;
          continue;
        }
        SetRuntimeReplacement(it->first, nullptr);
        REXLOG_INFO("Track signage: board {:016X} is the game's again", it->first);
        it = shown.erase(it);
      }
      SetShownCount(shown.size());
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
  }).detach();
  REXLOG_INFO("Track signage: watching {}", rex::path_to_utf8(folder));
}

}  // namespace fm4_static_recomp::signage
