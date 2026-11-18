// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
// CUSTOM DECALS (custom_decals.h): the package, the folder's images and the game's files.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/xam/content_manager.h>
#include <rex/ui/image_decode.h>

#include "custom_decals.h"
#include "ui/hostui/ui_archive.h"

REXCVAR_DECLARE(std::string, fm4_custom_decals_folder);
REXCVAR_DECLARE(bool, fm4_all_sponsor_decals);

namespace fm4_static_recomp::decals {
namespace {

constexpr const char* kIdsFile = "custom-decals.txt";

std::optional<std::vector<uint8_t>> ReadAll(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool WriteAll(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  const std::filesystem::path temp = path.string() + ".part";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!out) return false;
  }
  std::filesystem::rename(temp, path, ec);  // the game never sees half a file
  return !ec;
}

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(uint8_t(c)));
  return s;
}

// UTF-8 to UTF-16 (invalid bytes become U+FFFD).
std::u16string Utf16(std::string_view s) {
  std::u16string out;
  for (size_t i = 0; i < s.size();) {
    const uint8_t c = uint8_t(s[i]);
    uint32_t cp = 0xFFFD;
    size_t n = 1;
    if (c < 0x80) {
      cp = c;
    } else if ((c >> 5) == 6 && i + 1 < s.size()) {
      cp = (c & 0x1F) << 6 | (uint8_t(s[i + 1]) & 0x3F);
      n = 2;
    } else if ((c >> 4) == 14 && i + 2 < s.size()) {
      cp = (c & 0x0F) << 12 | (uint8_t(s[i + 1]) & 0x3F) << 6 | (uint8_t(s[i + 2]) & 0x3F);
      n = 3;
    } else if ((c >> 3) == 30 && i + 3 < s.size()) {
      cp = (c & 0x07) << 18 | (uint8_t(s[i + 1]) & 0x3F) << 12 | (uint8_t(s[i + 2]) & 0x3F) << 6 |
           (uint8_t(s[i + 3]) & 0x3F);
      n = 4;
    }
    i += n;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(char16_t(0xD800 + (cp >> 10)));
      out.push_back(char16_t(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back(char16_t(cp));
    }
  }
  return out;
}

// What InstallCustomDecals learnt at startup, for the builds after it.
struct Setup {
  std::mutex mutex;  // held by a build
  bool ready = false;
  std::filesystem::path decals_dir;  // the package folder the game mounts at GAME:\Media\Livery\Decals
  std::filesystem::path ids_path;
  std::vector<uint8_t> decal_template;
  std::set<uint16_t> taken_keys;
} g_setup;

// Watches the settings and the folder: a change (an image added, edited or taken out) makes the
// decals again, in the background.
struct Watcher {
  std::mutex mutex;
  std::condition_variable wake;
  bool stop = false;
  bool woken = false;
  std::thread thread;
  ~Watcher() {
    {
      std::lock_guard lock(mutex);
      stop = true;
    }
    wake.notify_all();
    if (thread.joinable()) thread.join();
  }
} g_watcher;

constexpr auto kWatchInterval = std::chrono::seconds(2);

// What a build is made from: the settings and each image's name, size and time.
std::string Signature() {
  const std::string folder = REXCVAR_GET(fm4_custom_decals_folder);
  std::string out = folder + (REXCVAR_GET(fm4_all_sponsor_decals) ? "\n1\n" : "\n0\n");
  if (folder.empty()) return out;
  std::vector<std::string> lines;
  std::error_code ec;
  for (std::filesystem::directory_iterator it(rex::to_path(folder), ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code item;
    if (!it->is_regular_file(item) || Lower(it->path().extension().string()) != ".png") continue;
    lines.push_back(rex::path_to_utf8(it->path().filename()) + "\t" + std::to_string(it->file_size(item)) + "\t" +
                    std::to_string(it->last_write_time(item).time_since_epoch().count()));
  }
  std::sort(lines.begin(), lines.end());
  for (const std::string& line : lines) out += line + "\n";
  return out;
}

// The package: a content package whose folder the game mounts at GAME:\ (its DLC zips folder,
// unpacked), with the installed header an FM4 marketplace package has. No rows, no strings: those
// come from the hooks, so files added to its folder later are all the game needs.
std::filesystem::path EnsurePackage(const std::filesystem::path& user_root) {
  const std::filesystem::path title = user_root / "0000000000000000" / "4D530910";
  const std::filesystem::path package = title / "00000002" / std::string(kPackageName);
  const std::filesystem::path header = title / "Headers" / "00000002" / (std::string(kPackageName) + ".header");
  const std::filesystem::path media = package / "Media";
  const std::string main = std::to_string(kPackageOffer) + "_pri_65";
  const std::filesystem::path decals = media / "DLCZips" / main / "Media" / "Livery" / "Decals";
  std::error_code ec;
  // An earlier layout carried a database patch and string tables: they are not needed now.
  for (const std::filesystem::path& old :
       {media / "DLCZips" / (main + ".zip"), media / "DLCZips" / main / "Media" / "db",
        media / "DLCZips" / "zipmount.xml", media / "DLCZips" / "stringtables.xml", package / "fm4customdecals.txt"}) {
    std::filesystem::remove_all(old, ec);
  }
  for (std::filesystem::directory_iterator it(media / "DLCZips", ec), end; !ec && it != end; it.increment(ec)) {
    if (it->path().filename().string().starts_with("StringTables_")) std::filesystem::remove_all(it->path(), ec);
  }
  ec.clear();
  std::filesystem::create_directories(decals, ec);
  if (ec) {
    REXLOG_ERROR("CUSTOM DECALS: cannot make {}: {}", rex::path_to_utf8(decals), ec.message());
    return {};
  }
  const std::filesystem::path offer = media / (std::to_string(kPackageOffer) + ".puboffer");
  if (!std::filesystem::exists(offer) && !WriteAll(offer, {0x20, 0x0d, 0x0a})) return {};
  if (!std::filesystem::exists(header)) {
    using rex::system::xam::XCONTENT_AGGREGATE_DATA;
    XCONTENT_AGGREGATE_DATA data{};
    data.device_id = 1;
    data.content_type = rex::system::XContentType::kMarketplaceContent;
    data.set_display_name(u"FM4 Custom Decals");
    data.set_file_name(std::string(kPackageName));
    data.title_id = 0x4D530910;
    data.xuid = 0;
    // The full license after the content data, as the recomp's own license repair writes it.
    std::vector<uint8_t> bytes(sizeof(data) + 4, 0xFF);
    std::memcpy(bytes.data(), &data, sizeof(data));
    if (!WriteAll(header, bytes)) {
      REXLOG_ERROR("CUSTOM DECALS: cannot write {}", rex::path_to_utf8(header));
      return {};
    }
  }
  return decals;
}

// Makes the decals of the settings now and publishes them; at startup also removes the files no
// decal uses any more (later the game may still have one open).
void Build(bool at_startup) {
  std::lock_guard lock(g_setup.mutex);
  const std::string folder = REXCVAR_GET(fm4_custom_decals_folder);
  const bool all_sponsors = REXCVAR_GET(fm4_all_sponsor_decals);
  Status status;
  std::vector<std::pair<std::string, std::filesystem::path>> images;
  if (!folder.empty()) {
    std::error_code ec;
    const std::filesystem::path root = rex::to_path(folder);
    if (!std::filesystem::is_directory(root, ec)) {
      REXLOG_WARN("CUSTOM DECALS: the folder {} is not there", folder);
      status.problems.push_back("The folder could not be found.");
    }
    for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
      if (it->is_regular_file(ec) && Lower(it->path().extension().string()) == ".png") {
        images.emplace_back(rex::path_to_utf8(it->path().stem()), it->path());
      }
    }
    std::sort(images.begin(), images.end());
  }

  // Ids, kept for good in the config folder.
  std::map<std::string, int> ids;
  if (const auto text = ReadAll(g_setup.ids_path)) {
    ids = ParseIds(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()));
  }
  std::vector<std::string> names;
  for (const auto& [name, path] : images) names.push_back(name);
  const size_t had = ids.size();
  ids = AssignIds(names, std::move(ids));
  if (ids.size() != had) {
    std::error_code ec;
    std::filesystem::create_directories(g_setup.ids_path.parent_path(), ec);
    std::ofstream(g_setup.ids_path, std::ios::binary | std::ios::trunc) << FormatIds(ids);
  }

  std::vector<LiveDecal> decals;
  std::set<std::string> files;
  if (g_setup.ready && !g_setup.decals_dir.empty()) {
    for (const auto& [name, path] : images) {
      if (!ids.contains(name)) {
        REXLOG_WARN("CUSTOM DECALS: {} left out (at most {} decals)", name, kMaxDecals);
        status.problems.push_back(rex::path_to_utf8(path.filename()) + " (over the limit of " +
                                  std::to_string(kMaxDecals) + " decals)");
        continue;
      }
      const int id = ids.at(name);
      const auto bytes = ReadAll(path);
      const std::string file = bytes ? DecalFileName(id, ImageHash(*bytes)) : std::string();
      std::error_code ec;
      const std::filesystem::path target = g_setup.decals_dir / file;
      // An image made before is reused: its file has its id and hash in its name.
      const bool made = bytes && std::filesystem::file_size(target, ec) == DecalFileSize(kCustomDecalSize);
      if (!made) {
        int w = 0, h = 0;
        const std::vector<uint8_t> rgba =
            bytes ? rex::ui::DecodeImageRGBA(bytes->data(), bytes->size(), w, h) : std::vector<uint8_t>{};
        if (rgba.empty()) {
          REXLOG_WARN("CUSTOM DECALS: {} is not a PNG image the game can read", rex::path_to_utf8(path));
          status.problems.push_back(rex::path_to_utf8(path.filename()) + " (not a valid PNG image)");
          continue;
        }
        const auto decal =
            EncodeDecal(FitImage(rgba, w, h, kCustomDecalSize), kCustomDecalSize, g_setup.decal_template);
        if (!WriteAll(target, decal)) {
          REXLOG_ERROR("CUSTOM DECALS: cannot write {}", rex::path_to_utf8(target));
          status.problems.push_back(rex::path_to_utf8(path.filename()) + " (could not be saved)");
          continue;
        }
      }
      files.insert(file);
      decals.push_back({id, name, file, 0});
    }
  } else if (!images.empty()) {
    status.problems.push_back("The game's own decals could not be read, so custom decals are unavailable.");
  }

  // Keys for the menu items' labels (ids 1 and 2, before every decal's) and the decals' names.
  constexpr int kApplyLabelId = 1, kChangeLabelId = 2;
  std::vector<int> decal_ids = {kApplyLabelId, kChangeLabelId};
  for (const LiveDecal& d : decals) decal_ids.push_back(d.id);
  const auto keys = AssignKeys(decal_ids, g_setup.taken_keys);
  std::map<uint16_t, std::u16string> shown;
  std::vector<LiveDecal> named;
  for (LiveDecal d : decals) {
    if (!keys.contains(d.id)) continue;
    d.key = keys.at(d.id);
    shown[d.key] = Utf16(d.name);
    named.push_back(std::move(d));
  }
  status.decals = int(named.size());
  if (!named.empty() && keys.contains(kApplyLabelId) && keys.contains(kChangeLabelId)) {
    status.apply_key = keys.at(kApplyLabelId);
    status.change_key = keys.at(kChangeLabelId);
    shown[status.apply_key] = std::u16string(kApplyLabel);
    shown[status.change_key] = std::u16string(kChangeLabel);
  }

  if (at_startup && !g_setup.decals_dir.empty()) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(g_setup.decals_dir, ec), end; !ec && it != end; it.increment(ec)) {
      if (!files.contains(rex::path_to_utf8(it->path().filename()))) std::filesystem::remove(it->path(), ec);
    }
  }
  Publish(DecalStatements(named, all_sponsors), std::move(shown), status);
  REXLOG_INFO("CUSTOM DECALS: {} decal{} from {}{}", named.size(), named.size() == 1 ? "" : "s",
              folder.empty() ? "no folder" : folder, all_sponsors ? ", all sponsor decals on every car" : "");
}

}  // namespace

void InstallCustomDecals(const std::filesystem::path& game_data_root, const std::filesystem::path& user_root,
                         const std::filesystem::path& config_root) {
  {
    std::lock_guard lock(g_setup.mutex);
    g_setup.ids_path = config_root / kIdsFile;
    g_setup.decals_dir = EnsurePackage(user_root);
    const auto livery = hostui::UiArchive::Open(game_data_root / "Media" / "Livery.zip");
    if (auto t = livery ? livery->Read("decals/decal002.carbin") : std::nullopt; t && IsDecalFile(*t)) {
      g_setup.decal_template = std::move(*t);
      g_setup.ready = true;
    } else {
      REXLOG_WARN("CUSTOM DECALS: the game's decals (Media\\Livery.zip) are not readable");
    }
    // Name keys the game's own decal names do not use, in any language.
    std::error_code ec;
    for (std::filesystem::directory_iterator it(game_data_root / "Media" / "StringTables", ec), end;
         !ec && it != end; it.increment(ec)) {
      if (Lower(it->path().extension().string()) != ".zip") continue;
      const auto zip = hostui::UiArchive::Open(it->path());
      if (const auto table = zip ? zip->Read("Livery_Decals.str") : std::nullopt) {
        if (const auto keys = StringHashes(*table)) g_setup.taken_keys.insert(keys->begin(), keys->end());
      }
    }
  }
  std::string built = Signature();
  Build(true);
  SetRebuild([] {
    {
      std::lock_guard lock(g_watcher.mutex);
      g_watcher.woken = true;
    }
    g_watcher.wake.notify_all();
  });
  g_watcher.thread = std::thread([built]() mutable {
    std::unique_lock lock(g_watcher.mutex);
    while (!g_watcher.stop) {
      g_watcher.wake.wait_for(lock, kWatchInterval, [] { return g_watcher.stop || g_watcher.woken; });
      if (g_watcher.stop) break;
      g_watcher.woken = false;
      lock.unlock();
      if (std::string now = Signature(); now != built) {
        SetBuilding();
        Build(false);
        built = std::move(now);
      }
      lock.lock();
    }
  });
}



}  // namespace fm4_static_recomp::decals
