// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "track_signage_runtime.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <cstdlib>
#endif

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/image_decode.h>

#include "config/host_config.h"
#include "ui/hostui/ui_archive.h"

REXCVAR_DECLARE(bool, fm4_track_signage);

namespace fm4_static_recomp::signage {
namespace {

using rex::graphics::texture_replacement::SetRuntimeReplacement;

constexpr uint32_t kPreviewWidth = 512, kPreviewHeight = 256;

struct FileStamp {
  std::filesystem::file_time_type time;
  uintmax_t size = 0;
  bool operator==(const FileStamp&) const = default;
};

struct LoadedPicture {
  FileStamp stamp;
  Picture picture;
  std::shared_ptr<const Image> full;  // decoded only while a board shows it
};

struct State {
  std::mutex mutex;
  std::condition_variable wake;
  std::filesystem::path folder;
  std::filesystem::path game_root;
  std::filesystem::path choices_file;
  Choices choices;
  std::map<std::string, LoadedPicture> pictures;  // by file name (lower case)
  std::map<std::string, std::shared_ptr<const Image>> previews;  // ChoiceKey -> the game's board
  std::set<std::string> preview_wanted;
  size_t shown = 0;
  bool started = false;
};

State& GetState() {
  static State state;
  return state;
}

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
  return text;
}

std::optional<std::vector<uint8_t>> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::optional<Image> DecodePng(const std::filesystem::path& path) {
  const auto bytes = ReadFile(path);
  if (!bytes || bytes->empty()) return std::nullopt;
  int width = 0, height = 0;
  std::vector<uint8_t> rgba = rex::ui::DecodeImageRGBA(bytes->data(), bytes->size(), width, height);
  if (rgba.empty() || width <= 0 || height <= 0) return std::nullopt;
  return Image{uint32_t(width), uint32_t(height), std::move(rgba)};
}

// The folder's PNGs and their stamps.
std::map<std::string, std::pair<std::filesystem::path, FileStamp>> ScanFolder(const std::filesystem::path& folder) {
  std::map<std::string, std::pair<std::filesystem::path, FileStamp>> found;
  std::error_code ec;
  for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code item;
    if (!it->is_regular_file(item)) continue;
    const std::string name = rex::path_to_utf8(it->path().filename());
    if (Lower(rex::path_to_utf8(it->path().extension())) != ".png") continue;
    FileStamp stamp{it->last_write_time(item), it->file_size(item)};
    if (item) continue;
    found[Lower(name)] = {it->path(), stamp};
  }
  return found;
}

// The game's board from its track archive, as a preview.
std::shared_ptr<const Image> DecodeBoard(const hostui::UiArchive* archive, const Board& board) {
  if (!archive) return nullptr;
  char member[32];
  std::snprintf(member, sizeof(member), "_0x%08X_B.bix", board.texture);
  const auto data = archive->Read(member);
  if (!data) return nullptr;
  return std::make_shared<Image>(Fit(DecodeTiledDxt5(*data, board.width, board.height), kPreviewWidth, kPreviewHeight));
}

void Run() {
  State& state = GetState();
  std::map<uint64_t, std::pair<std::string, FileStamp>> applied;  // board hash -> picture shown
  std::set<std::string> warned;
  for (;;) {
    std::filesystem::path folder, game_root;
    Choices choices;
    std::set<std::string> wanted;
    {
      std::lock_guard lock(state.mutex);
      folder = state.folder;
      game_root = state.game_root;
      choices = state.choices;
      wanted = state.preview_wanted;
    }
    // The folder: new and changed pictures get a thumbnail; gone ones are forgotten.
    const auto found = ScanFolder(folder);
    std::map<std::string, LoadedPicture> pictures;
    {
      std::lock_guard lock(state.mutex);
      pictures = state.pictures;
    }
    for (auto it = pictures.begin(); it != pictures.end();) {
      it = found.contains(it->first) ? std::next(it) : pictures.erase(it);
    }
    for (const auto& [key, entry] : found) {
      const auto& [path, stamp] = entry;
      auto known = pictures.find(key);
      if (known != pictures.end() && known->second.stamp == stamp) continue;
      LoadedPicture loaded{stamp, Picture{rex::path_to_utf8(path.filename())}, nullptr};
      if (const auto image = DecodePng(path)) {
        loaded.picture.width = image->width;
        loaded.picture.height = image->height;
        loaded.picture.thumbnail = std::make_shared<Image>(Fit(*image, kPreviewWidth, kPreviewHeight));
      } else if (warned.insert(key).second) {
        REXLOG_WARN("Track signage: {} is not a PNG it can read", loaded.picture.file);
      }
      pictures[key] = std::move(loaded);
    }
    // The boards: each chosen picture that exists (with TRACK SIGNAGE on), the game's otherwise.
    std::map<uint64_t, std::pair<const Board*, std::string>> desired;
    if (REXCVAR_GET(fm4_track_signage)) {
      for (const Track& track : Tracks()) {
        for (const Board& board : track.boards) {
          const auto choice = choices.find(ChoiceKey(track, board));
          if (choice == choices.end()) continue;
          const auto picture = pictures.find(Lower(choice->second));
          if (picture == pictures.end() || !picture->second.picture.width) continue;
          desired[board.hash] = {&board, picture->first};
        }
      }
    }
    for (const auto& [hash, want] : desired) {
      const auto& [board, key] = want;
      LoadedPicture& picture = pictures[key];
      const auto shown = applied.find(hash);
      if (shown != applied.end() && shown->second.first == key && shown->second.second == picture.stamp) continue;
      if (!picture.full) {
        const auto image = DecodePng(found.at(key).first);
        if (!image) continue;
        picture.full = std::make_shared<Image>(*image);
      }
      const auto image = BuildImage(*board, *picture.full);
      SetRuntimeReplacement(hash, image);
      applied[hash] = {key, picture.stamp};
      REXLOG_INFO("Track signage: {} on {} ({} x {}, {} levels)", picture.picture.file, board->name, image->width,
                  image->height, image->levels);
    }
    for (auto it = applied.begin(); it != applied.end();) {
      if (desired.contains(it->first)) {
        ++it;
        continue;
      }
      SetRuntimeReplacement(it->first, nullptr);
      REXLOG_INFO("Track signage: board {:016X} shows the game's again", it->first);
      it = applied.erase(it);
    }
    // Full images are kept only while a board shows them.
    std::set<std::string> in_use;
    for (const auto& [hash, want] : applied) in_use.insert(want.first);
    for (auto& [key, picture] : pictures) {
      if (!in_use.contains(key)) picture.full.reset();
    }
    // Previews the page asked for.
    std::map<std::string, std::shared_ptr<const Image>> previews;
    std::map<std::string_view, std::optional<hostui::UiArchive>> archives;  // opened once a pass
    for (const std::string& key : wanted) {
      const size_t dot = key.find('.');
      const Track* track = FindTrack(key.substr(0, dot));
      const Board* board = track ? FindBoard(*track, key.substr(dot + 1)) : nullptr;
      if (!board) continue;
      auto archive = archives.find(track->archive);
      if (archive == archives.end()) {
        archive = archives
                      .emplace(track->archive,
                               hostui::UiArchive::Open(game_root / rex::to_path(std::string(track->archive))))
                      .first;
      }
      previews[key] = DecodeBoard(archive->second ? &*archive->second : nullptr, *board);
    }
    {
      std::unique_lock lock(state.mutex);
      state.pictures = std::move(pictures);
      for (auto& [key, image] : previews) {
        state.previews[key] = image;
        state.preview_wanted.erase(key);
      }
      state.shown = applied.size();
      state.wake.wait_for(lock, std::chrono::seconds(1));
    }
  }
}

}  // namespace

void Start(const std::filesystem::path& folder, const std::filesystem::path& game_root,
           const std::filesystem::path& choices_file) {
  State& state = GetState();
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  const auto text = ReadFile(choices_file);
  {
    std::lock_guard lock(state.mutex);
    if (state.started) return;
    state.started = true;
    state.folder = folder;
    state.game_root = game_root;
    state.choices_file = choices_file;
    const std::string_view view = text ? std::string_view(reinterpret_cast<const char*>(text->data()), text->size())
                                       : std::string_view();
    state.choices = ParseChoices(view);
    // TRACK SIGNAGE off is saved beside the choices; a launch option wins.
    if (config::GetValue(view, "fm4_track_signage") == std::optional<std::string>("false") &&
        rex::cvar::GetFlagSource("fm4_track_signage") != rex::cvar::Source::kCommandLine) {
      rex::cvar::SetFlagByName("fm4_track_signage", "false");
    }
  }
  std::thread(Run).detach();
  REXLOG_INFO("Track signage: {} choices from {}, pictures from {}", state.choices.size(),
              rex::path_to_utf8(choices_file), rex::path_to_utf8(folder));
}

std::filesystem::path Folder() {
  State& state = GetState();
  std::lock_guard lock(state.mutex);
  return state.folder;
}

bool OpenFolder() {
  const auto folder = Folder();
  if (folder.empty()) return false;
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
#ifdef _WIN32
  const auto result = ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  const bool ok = reinterpret_cast<INT_PTR>(result) > 32;
#else
#ifdef __APPLE__
  const std::string command = "open \"" + rex::path_to_utf8(folder) + "\" &";
#else
  const std::string command = "xdg-open \"" + rex::path_to_utf8(folder) + "\" &";
#endif
  const bool ok = std::system(command.c_str()) == 0;
#endif
  REXLOG_INFO("Track signage: {} the folder {}", ok ? "opened" : "could not open", rex::path_to_utf8(folder));
  return ok;
}

std::vector<Picture> Pictures() {
  State& state = GetState();
  std::lock_guard lock(state.mutex);
  std::vector<Picture> out;
  for (const auto& [key, picture] : state.pictures) {
    if (picture.picture.width) out.push_back(picture.picture);
  }
  return out;
}

std::shared_ptr<const Image> BoardPreview(const Track& track, const Board& board) {
  State& state = GetState();
  const std::string key = ChoiceKey(track, board);
  std::lock_guard lock(state.mutex);
  const auto found = state.previews.find(key);
  if (found != state.previews.end()) return found->second;
  if (state.preview_wanted.insert(key).second) state.wake.notify_one();
  return nullptr;
}

Choices CurrentChoices() {
  State& state = GetState();
  std::lock_guard lock(state.mutex);
  return state.choices;
}

bool SaveChoices(const Choices& choices, bool enabled) {
  State& state = GetState();
  std::filesystem::path file;
  {
    std::lock_guard lock(state.mutex);
    file = state.choices_file;
  }
  bool saved = true;
  if (!file.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::string text = FormatChoices(choices);
    if (!enabled) text += "fm4_track_signage = false\n";
    saved = config::WriteAtomically(file, text);
  }
  if (!saved) {
    REXLOG_ERROR("Track signage: could not write {}", rex::path_to_utf8(file));
    return false;
  }
  if (rex::cvar::GetFlagSource("fm4_track_signage") != rex::cvar::Source::kCommandLine) {
    rex::cvar::SetFlagByName("fm4_track_signage", enabled ? "true" : "false");
  }
  {
    std::lock_guard lock(state.mutex);
    state.choices = choices;
  }
  state.wake.notify_one();
  REXLOG_INFO("Track signage: {} choices saved", choices.size());
  return true;
}

size_t ShownCount() {
  State& state = GetState();
  std::lock_guard lock(state.mutex);
  return state.shown;
}

}  // namespace fm4_static_recomp::signage
