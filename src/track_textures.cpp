// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "track_textures.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <xxhash.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/image_decode.h>

#include "track_signage_runtime.h"
#include "ui/hostui/ui_archive.h"

REXCVAR_DEFINE_STRING(fm4_track_textures, "default", "Forza Motorsport 4",
                      "TRACK TEXTURES (PC OPTIONS > MODS): default, 2x or 4x: the track's textures upscaled from "
                      "your own disc (Hockenheimring for now)");
REXCVAR_DEFINE_STRING(fm4_road_surface, "default", "Forza Motorsport 4",
                      "ROAD SURFACE (PC OPTIONS > MODS): default, 2x or 4x: the road alone, over TRACK TEXTURES");
REXCVAR_DEFINE_STRING(fm4_grass_texture, "default", "Forza Motorsport 4",
                      "GRASS (PC OPTIONS > MODS): default, 2x or 4x: the grass alone, over TRACK TEXTURES");
REXCVAR_DECLARE(bool, fm4_track_signage);

namespace fm4_static_recomp::track_textures {
namespace {

using rex::graphics::texture_replacement::RuntimeImage;
using rex::graphics::texture_replacement::SetRuntimeReplacement;
using signage::Image;

// The upscaler, pinned: Real-ESRGAN's Windows build (realesrgan-ncnn-vulkan, with its models).
constexpr const char* kUpscalerUrl =
    "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip";
constexpr const char* kUpscalerSha256 = "ABC02804E17982A3BE33675E4D471E91EA374E65B70167ABC09E31ACB412802D";
constexpr const char* kUpscalerExe = "realesrgan-ncnn-vulkan.exe";
constexpr const char* kTrackArchive = "Media/Tracks/Hockenheim/bin.zip";
constexpr const char* kTrackKey = "hockenheim";
constexpr uint32_t kMinSide = 128;  // smaller textures gain nothing worth the time
constexpr uint32_t kBorder = 32;    // wrapped edge around a texture while it is upscaled

// Rec. 601 luma, 0..255.
int Luma(const uint8_t* p) { return (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000; }

uint32_t Adler32(const std::vector<uint8_t>& data) {
  uint32_t a = 1, b = 0;
  for (const uint8_t byte : data) {
    a = (a + byte) % 65521;
    b = (b + a) % 65521;
  }
  return b << 16 | a;
}

void PutBe32(std::vector<uint8_t>& out, uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) out.push_back(uint8_t(v >> s));
}

void Chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
  PutBe32(png, uint32_t(data.size()));
  std::vector<uint8_t> body(type, type + 4);
  body.insert(body.end(), data.begin(), data.end());
  png.insert(png.end(), body.begin(), body.end());
  PutBe32(png, hostui::Crc32(body));
}

std::string Upper(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::toupper(c)); });
  return text;
}

}  // namespace

std::optional<Scale> ParseScale(std::string_view value) {
  if (value == "default") return Scale::kDefault;
  if (value == "2x") return Scale::k2x;
  if (value == "4x") return Scale::k4x;
  return std::nullopt;
}

std::string_view ScaleName(Scale scale) {
  return scale == Scale::k2x ? "2x" : scale == Scale::k4x ? "4x" : "default";
}

std::string ScaleLabel(Scale scale) {
  return scale == Scale::k2x ? "2X" : scale == Scale::k4x ? "4X" : "DEFAULT";
}

bool WorthUpscaling(const Image& image) {
  const size_t n = size_t(image.width) * image.height;
  if (!n) return false;
  double sum[3] = {};
  for (size_t i = 0; i < n; ++i) {
    for (int c = 0; c < 3; ++c) sum[c] += image.rgba[i * 4 + c];
  }
  const double r = sum[0] / n, g = sum[1] / n, b = sum[2] / n;
  if (std::fabs(r - 128) < 30 && std::fabs(g - 128) < 30 && b > 180) return false;  // a normal map
  if ((r + g + b) / 3 > 225) return false;                                         // a mask, mostly white
  // Fine detail: how far each texel stands from its 3x3 neighbourhood's mean.
  double spread = 0;
  size_t count = 0;
  for (uint32_t y = 1; y + 1 < image.height; y += 2) {
    for (uint32_t x = 1; x + 1 < image.width; x += 2) {
      int mean = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) mean += Luma(&image.rgba[((y + dy) * size_t(image.width) + x + dx) * 4]);
      }
      const double d = Luma(&image.rgba[(y * size_t(image.width) + x) * 4]) - mean / 9.0;
      spread += d * d;
      ++count;
    }
  }
  return count && std::sqrt(spread / count) >= 4.0;
}

Image WrapPad(const Image& image, uint32_t border) {
  const uint32_t w = image.width + 2 * border, h = image.height + 2 * border;
  Image out{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
  for (uint32_t y = 0; y < h; ++y) {
    const uint32_t sy = (y + image.height * 4 - border) % image.height;
    for (uint32_t x = 0; x < w; ++x) {
      const uint32_t sx = (x + image.width * 4 - border) % image.width;
      std::memcpy(&out.rgba[(size_t(y) * w + x) * 4], &image.rgba[(size_t(sy) * image.width + sx) * 4], 4);
    }
  }
  return out;
}

Image Crop(const Image& image, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
  Image out{width, height, std::vector<uint8_t>(size_t(width) * height * 4)};
  for (uint32_t row = 0; row < height; ++row) {
    std::memcpy(&out.rgba[size_t(row) * width * 4], &image.rgba[((size_t(y) + row) * image.width + x) * 4],
                size_t(width) * 4);
  }
  return out;
}

void MatchColours(Image& image, const Image& reference) {
  for (int c = 0; c < 3; ++c) {
    std::array<double, 256> have{}, want{};
    for (size_t i = c; i < image.rgba.size(); i += 4) have[image.rgba[i]] += 1;
    for (size_t i = c; i < reference.rgba.size(); i += 4) want[reference.rgba[i]] += 1;
    for (int v = 1; v < 256; ++v) {
      have[v] += have[v - 1];
      want[v] += want[v - 1];
    }
    std::array<uint8_t, 256> map{};
    int to = 0;
    for (int v = 0; v < 256; ++v) {
      const double at = have[v] / have[255];
      while (to < 255 && want[to] / want[255] < at) ++to;
      map[v] = uint8_t(to);
    }
    for (size_t i = c; i < image.rgba.size(); i += 4) image.rgba[i] = map[image.rgba[i]];
  }
}

std::vector<uint8_t> EncodePng(const Image& image) {
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> header;
  PutBe32(header, image.width);
  PutBe32(header, image.height);
  header.insert(header.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA
  Chunk(png, "IHDR", header);
  std::vector<uint8_t> raw;
  raw.reserve((size_t(image.width) * 4 + 1) * image.height);
  for (uint32_t y = 0; y < image.height; ++y) {
    raw.push_back(0);  // no filter
    const auto row = image.rgba.begin() + ptrdiff_t(size_t(y) * image.width * 4);
    raw.insert(raw.end(), row, row + ptrdiff_t(image.width) * 4);
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  for (size_t at = 0; at < raw.size() || at == 0;) {
    const size_t len = std::min<size_t>(65535, raw.size() - at);
    const bool last = at + len >= raw.size();
    z.push_back(last ? 1 : 0);
    z.insert(z.end(), {uint8_t(len), uint8_t(len >> 8), uint8_t(~len), uint8_t(~len >> 8)});
    z.insert(z.end(), raw.begin() + ptrdiff_t(at), raw.begin() + ptrdiff_t(at + len));
    at += len;
    if (last) break;
  }
  PutBe32(z, Adler32(raw));
  Chunk(png, "IDAT", z);
  Chunk(png, "IEND", {});
  return png;
}

std::shared_ptr<const RuntimeImage> Finish(const Image& original, const Image& upscaled, uint32_t border,
                                           Scale scale, bool dxt1) {
  Image image = Crop(upscaled, border * 4, border * 4, original.width * 4, original.height * 4);
  MatchColours(image, original);
  // The original's alpha (cut-out edges, blend masks), at the new size.
  const Image alpha = signage::Resample(original, image.width, image.height);
  for (size_t i = 3; i < image.rgba.size(); i += 4) image.rgba[i] = alpha.rgba[i];
  const uint32_t s = uint32_t(scale);
  return signage::BuildTexture(image, original.width * s, original.height * s, dxt1);
}

std::vector<uint8_t> SerializeImage(const RuntimeImage& image) {
  std::vector<uint8_t> out = {'F', 'M', '4', 'T', 'E', 'X', '1', 0};
  for (const uint32_t v : {uint32_t(image.format), image.width, image.height, image.levels, uint32_t(image.data.size())}) {
    for (int s = 0; s < 32; s += 8) out.push_back(uint8_t(v >> s));
  }
  out.insert(out.end(), image.data.begin(), image.data.end());
  return out;
}

std::shared_ptr<const RuntimeImage> DeserializeImage(std::span<const uint8_t> data) {
  if (data.size() < 28 || std::memcmp(data.data(), "FM4TEX1", 8) != 0) return nullptr;
  uint32_t v[5];
  for (int i = 0; i < 5; ++i) {
    v[i] = 0;
    for (int s = 0; s < 4; ++s) v[i] |= uint32_t(data[8 + i * 4 + s]) << (8 * s);
  }
  if (data.size() != 28 + size_t(v[4]) || !v[1] || !v[2] || !v[3]) return nullptr;
  auto image = std::make_shared<RuntimeImage>();
  image->format = rex::graphics::xenos::TextureFormat(v[0]);
  image->width = v[1];
  image->height = v[2];
  image->levels = v[3];
  image->data.assign(data.begin() + 28, data.end());
  return image;
}

namespace {

constexpr const char* kNames[] = {"fm4_track_textures", "fm4_road_surface", "fm4_grass_texture"};

bool FromCommandLine(const char* name) {
  return rex::cvar::GetFlagSource(name) == rex::cvar::Source::kCommandLine;
}

std::mutex g_settings_mutex;
std::filesystem::path g_settings_file;

Scale* Field(Settings& settings, size_t i) { return i == 0 ? &settings.track : i == 1 ? &settings.road : &settings.grass; }

}  // namespace

std::string FormatSettings(const Settings& settings) {
  std::string text;
  Settings copy = settings;
  for (size_t i = 0; i < 3; ++i) {
    const Scale scale = *Field(copy, i);
    if (scale != Scale::kDefault) text += std::string(kNames[i]) + " = \"" + std::string(ScaleName(scale)) + "\"\n";
  }
  return text;
}

Settings ParseSettings(std::string_view text) {
  Settings settings;
  size_t at = 0;
  while (at < text.size()) {
    const size_t end = std::min(text.find('\n', at), text.size());
    std::string_view line = text.substr(at, end - at);
    at = end + 1;
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    auto trim = [](std::string_view v) {
      while (!v.empty() && (v.front() == ' ' || v.front() == '"')) v.remove_prefix(1);
      while (!v.empty() && (v.back() == ' ' || v.back() == '"' || v.back() == '\r')) v.remove_suffix(1);
      return v;
    };
    const std::string_view key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
    for (size_t i = 0; i < 3; ++i) {
      if (key == kNames[i]) {
        if (const auto scale = ParseScale(value)) *Field(settings, i) = *scale;
      }
    }
  }
  return settings;
}

Settings CurrentSettings() {
  Settings settings;
  for (size_t i = 0; i < 3; ++i) *Field(settings, i) = ParseScale(rex::cvar::GetFlagByName(kNames[i])).value_or(Scale::kDefault);
  return settings;
}

void SetSettingsFile(const std::filesystem::path& file) {
  std::lock_guard lock(g_settings_mutex);
  g_settings_file = file;
}

bool SaveSettings(const Settings& settings) {
  std::filesystem::path file;
  {
    std::lock_guard lock(g_settings_mutex);
    file = g_settings_file;
  }
  if (!file.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << FormatSettings(settings);
    if (!out) {
      REXLOG_ERROR("Track textures: could not write {}", rex::path_to_utf8(file));
      return false;
    }
  }
  Settings copy = settings;
  for (size_t i = 0; i < 3; ++i) {
    if (FromCommandLine(kNames[i])) continue;
    rex::cvar::SetFlagByName(kNames[i], std::string(ScaleName(*Field(copy, i))));
  }
  REXLOG_INFO("Track textures: TRACK TEXTURES {}, ROAD SURFACE {}, GRASS {}", ScaleName(settings.track),
              ScaleName(settings.road), ScaleName(settings.grass));
  Apply();
  return true;
}

// ---- The running side ----
namespace {

struct Texture {
  std::string member;  // the archive's _B.bix (lowercase)
  uint64_t hash = 0;
  uint32_t width = 0, height = 0;
  bool dxt1 = false;
  bool worth = false;  // TRACK TEXTURES covers it
};

enum class Phase { kIdle, kDownloading, kUpscaling, kFailed };

struct State {
  std::mutex mutex;
  std::condition_variable wake;
  std::thread worker;
  bool started = false, stop = false, pending = false;
  std::filesystem::path state_root, game_root;
  // Read by the page (any thread), under the mutex.
  Phase phase = Phase::kIdle;
  Scale working_scale = Scale::kDefault;
  size_t done = 0, total = 0;
  std::string error;
  size_t shown_track = 0;
  std::map<uint64_t, std::shared_ptr<const RuntimeImage>> shown;  // what this feature shows now
  std::map<uint64_t, Scale> shown_scale;
#ifdef _WIN32
  HANDLE process = nullptr;
#endif
};

State& GetState() {
  static State state;
  return state;
}

Scale Setting(const char* name) {
  return ParseScale(rex::cvar::GetFlagByName(name)).value_or(Scale::kDefault);
}

// Each texture's size as the settings want it now.
Scale Wanted(const Texture& texture) {
  if (texture.hash == kRoadHash && Setting("fm4_road_surface") != Scale::kDefault) return Setting("fm4_road_surface");
  if (texture.hash == kGrassHash && Setting("fm4_grass_texture") != Scale::kDefault) return Setting("fm4_grass_texture");
  return texture.worth || texture.hash == kRoadHash || texture.hash == kGrassHash ? Setting("fm4_track_textures")
                                                                                  : Scale::kDefault;
}

std::filesystem::path CachePath(const State& state, Scale scale, uint64_t hash) {
  char name[32];
  std::snprintf(name, sizeof(name), "%016llX.tex", static_cast<unsigned long long>(hash));
  return state.state_root / "track-textures" / kTrackKey / std::string(ScaleName(scale)) / name;
}

std::optional<std::vector<uint8_t>> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
  return bool(out);
}

std::optional<Image> Decode(const hostui::UiArchive& archive, const Texture& texture) {
  const auto data = archive.Read(texture.member);
  if (!data) return std::nullopt;
  return texture.dxt1 ? signage::DecodeTiledDxt1(*data, texture.width, texture.height)
                      : signage::DecodeTiledDxt5(*data, texture.width, texture.height);
}

// The track's textures: every DXT1 / DXT5 texture with its hash, and whether TRACK TEXTURES covers it.
std::vector<Texture> Inventory(const hostui::UiArchive& archive) {
  std::vector<Texture> out;
  for (const std::string& name : archive.MemberNames()) {
    if (!name.ends_with(".bix") || name.ends_with("_b.bix")) continue;
    const auto header = archive.Read(name);
    if (!header || header->size() < 20 || std::memcmp(header->data(), "BIX1", 4) != 0) continue;
    const auto be = [&](size_t at) {
      return uint32_t((*header)[at]) << 24 | uint32_t((*header)[at + 1]) << 16 | uint32_t((*header)[at + 2]) << 8 |
             (*header)[at + 3];
    };
    const uint8_t format = (*header)[19];
    if (format != 0x52 && format != 0x54) continue;  // DXT1, DXT5
    Texture texture{name.substr(0, name.size() - 4) + "_b.bix", 0, be(4), be(8), format == 0x52};
    const auto big = archive.Read(texture.member);
    if (!big) continue;
    texture.hash = XXH3_64bits(big->data(), big->size());
    if (std::max(texture.width, texture.height) >= kMinSide) {
      const Image image = texture.dxt1 ? signage::DecodeTiledDxt1(*big, texture.width, texture.height)
                                       : signage::DecodeTiledDxt5(*big, texture.width, texture.height);
      texture.worth = WorthUpscaling(image);
    }
    out.push_back(std::move(texture));
  }
  return out;
}

#ifdef _WIN32
// Runs a command line hidden; false on failure or when stopped.
bool Run(State& state, std::wstring command, const std::filesystem::path& cwd, const std::function<void()>& tick) {
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
    return false;
  }
  CloseHandle(pi.hThread);
  {
    std::lock_guard lock(state.mutex);
    state.process = pi.hProcess;
  }
  DWORD code = 1;
  for (;;) {
    if (WaitForSingleObject(pi.hProcess, 500) == WAIT_OBJECT_0) break;
    if (tick) tick();
    std::lock_guard lock(state.mutex);
    if (state.stop) {
      TerminateProcess(pi.hProcess, 1);
      break;
    }
  }
  GetExitCodeProcess(pi.hProcess, &code);
  {
    std::lock_guard lock(state.mutex);
    state.process = nullptr;
  }
  CloseHandle(pi.hProcess);
  return code == 0;
}
#endif

void SetPhase(State& state, Phase phase, std::string error = {}) {
  std::lock_guard lock(state.mutex);
  state.phase = phase;
  state.error = std::move(error);
}

// The upscaler, downloaded and checked once.
std::optional<std::filesystem::path> Upscaler(State& state) {
  const std::filesystem::path dir = state.state_root / "tools" / "realesrgan";
  const std::filesystem::path exe = dir / kUpscalerExe;
  std::error_code ec;
  if (std::filesystem::exists(exe, ec)) return exe;
#ifdef _WIN32
  SetPhase(state, Phase::kDownloading);
  std::filesystem::create_directories(dir, ec);
  const std::wstring d = dir.wstring();
  std::wstring script = L"$ErrorActionPreference='Stop'; $z=Join-Path '" + d + L"' 'upscaler.zip'; "
                        L"Invoke-WebRequest -UseBasicParsing '" + rex::to_path(kUpscalerUrl).wstring() +
                        L"' -OutFile $z; if ((Get-FileHash $z -Algorithm SHA256).Hash -ne '" +
                        rex::to_path(kUpscalerSha256).wstring() + L"') { Remove-Item $z; exit 2 }; "
                        L"Expand-Archive -Force $z '" + d + L"'; Remove-Item $z";
  const bool ok = Run(state, L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"" + script + L"\"", {}, {});
  if (ok && std::filesystem::exists(exe, ec)) {
    REXLOG_INFO("Track textures: upscaler downloaded to {}", rex::path_to_utf8(dir));
    return exe;
  }
  REXLOG_WARN("Track textures: could not download the upscaler");
#endif
  return std::nullopt;
}

// Shows each texture at the size wanted when its result is made; the game's own otherwise. A texture showing a
// picture of the player's own (TRACK SIGNAGE) keeps it.
void Show(State& state, const std::vector<Texture>& textures) {
  size_t track = 0;
  for (const Texture& texture : textures) {
    const Scale scale = Wanted(texture);
    {
      std::lock_guard lock(state.mutex);
      const auto known = state.shown_scale.find(texture.hash);
      const Scale now = known != state.shown_scale.end() ? known->second : Scale::kDefault;
      if (now == scale) {
        if (scale != Scale::kDefault && texture.worth) ++track;
        continue;
      }
    }
    std::shared_ptr<const RuntimeImage> image;
    if (scale != Scale::kDefault) {
      if (const auto data = ReadFile(CachePath(state, scale, texture.hash))) image = DeserializeImage(*data);
      if (!image) continue;  // not made yet: what shows stays until it is
    }
    {
      std::lock_guard lock(state.mutex);
      if (image) {
        state.shown[texture.hash] = image;
        state.shown_scale[texture.hash] = scale;
      } else {
        state.shown.erase(texture.hash);
        state.shown_scale.erase(texture.hash);
      }
    }
    if (image && texture.worth) ++track;
    if (!signage::ShowsPicture(texture.hash)) SetRuntimeReplacement(texture.hash, image);
  }
  std::lock_guard lock(state.mutex);
  state.shown_track = track;
}

void Pass(State& state, std::vector<Texture>& textures, const hostui::UiArchive& archive) {
  Show(state, textures);
  // What is wanted but not made yet.
  std::vector<const Texture*> missing;
  Scale largest = Scale::kDefault;
  for (const Texture& texture : textures) {
    const Scale scale = Wanted(texture);
    std::error_code ec;
    if (scale == Scale::kDefault || std::filesystem::exists(CachePath(state, scale, texture.hash), ec)) continue;
    missing.push_back(&texture);
    largest = std::max(largest, scale);
  }
  if (missing.empty()) {
    SetPhase(state, Phase::kIdle);
    return;
  }
#ifdef _WIN32
  const auto exe = Upscaler(state);
  if (!exe) {
    SetPhase(state, Phase::kFailed, "the upscaler could not be downloaded");
    return;
  }
  const std::filesystem::path work = state.state_root / "track-textures" / "work";
  const std::filesystem::path in = work / "in", out = work / "out";
  std::error_code ec;
  std::filesystem::remove_all(work, ec);
  std::filesystem::create_directories(in, ec);
  std::filesystem::create_directories(out, ec);
  std::map<std::string, std::pair<const Texture*, Image>> originals;
  for (const Texture* texture : missing) {
    auto image = Decode(archive, *texture);
    if (!image) continue;
    const uint32_t border = std::min({kBorder, image->width / 2, image->height / 2});
    char name[32];
    std::snprintf(name, sizeof(name), "%016llX.png", static_cast<unsigned long long>(texture->hash));
    WriteFile(in / name, EncodePng(WrapPad(*image, border)));
    originals[name] = {texture, std::move(*image)};
  }
  {
    std::lock_guard lock(state.mutex);
    state.phase = Phase::kUpscaling;
    state.working_scale = largest;
    state.done = 0;
    state.total = originals.size();
  }
  REXLOG_INFO("Track textures: upscaling {} textures", originals.size());
  const auto count_done = [&] {
    size_t done = 0;
    std::error_code e;
    for (std::filesystem::directory_iterator it(out, e), end; !e && it != end; it.increment(e)) ++done;
    std::lock_guard lock(state.mutex);
    state.done = done;
  };
  const std::wstring command = L"\"" + exe->wstring() + L"\" -i \"" + in.wstring() + L"\" -o \"" + out.wstring() +
                               L"\" -n realesrgan-x4plus -s 4 -f png";
  const bool ok = Run(state, command, exe->parent_path(), count_done);
  size_t made = 0;
  for (const auto& [name, entry] : originals) {
    const auto& [texture, original] = entry;
    const auto data = ReadFile(out / name);
    if (!data) continue;
    int w = 0, h = 0;
    const std::vector<uint8_t> rgba = rex::ui::DecodeImageRGBA(data->data(), data->size(), w, h);
    if (rgba.empty()) continue;
    const Image upscaled{uint32_t(w), uint32_t(h), rgba};
    const uint32_t border = std::min({kBorder, original.width / 2, original.height / 2});
    if (upscaled.width != (original.width + 2 * border) * 4) continue;
    const Scale scale = Wanted(*texture);
    if (scale == Scale::kDefault) continue;
    const auto image = Finish(original, upscaled, border, scale, texture->dxt1);
    if (WriteFile(CachePath(state, scale, texture->hash), SerializeImage(*image))) ++made;
  }
  std::filesystem::remove_all(work, ec);
  REXLOG_INFO("Track textures: {} of {} textures made", made, originals.size());
  {
    std::lock_guard lock(state.mutex);
    if (state.stop) return;
  }
  Show(state, textures);
  SetPhase(state, ok ? Phase::kIdle : Phase::kFailed, ok ? "" : "the upscaler stopped");
#endif
}

void Worker() {
  State& state = GetState();
  std::optional<hostui::UiArchive> archive;
  std::vector<Texture> textures;
  for (;;) {
    {
      std::unique_lock lock(state.mutex);
      state.wake.wait(lock, [&] { return state.stop || state.pending; });
      if (state.stop) return;
      state.pending = false;
    }
    if (!archive) {
      archive = hostui::UiArchive::Open(state.game_root / rex::to_path(kTrackArchive));
      if (!archive) {
        SetPhase(state, Phase::kFailed, "Hockenheimring's textures are not in the game folder");
        continue;
      }
      textures = Inventory(*archive);
      REXLOG_INFO("Track textures: Hockenheimring, {} textures, {} worth upscaling", textures.size(),
                  std::count_if(textures.begin(), textures.end(), [](const Texture& t) { return t.worth; }));
    }
    Pass(state, textures, *archive);
  }
}

}  // namespace

void Start(const std::filesystem::path& state_root, const std::filesystem::path& game_root,
           const std::filesystem::path& settings_file) {
  SetSettingsFile(settings_file);
  if (const auto data = ReadFile(settings_file)) {
    Settings saved = ParseSettings(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()));
    for (size_t i = 0; i < 3; ++i) {
      if (FromCommandLine(kNames[i])) {
        REXLOG_INFO("Track textures: {} comes from the command line", kNames[i]);
        continue;
      }
      rex::cvar::SetFlagByName(kNames[i], std::string(ScaleName(*Field(saved, i))));
    }
  }
  State& state = GetState();
  std::lock_guard lock(state.mutex);
  if (state.started) return;
  state.started = true;
  state.stop = false;
  state.state_root = state_root;
  state.game_root = game_root;
  state.pending = true;
  state.worker = std::thread(Worker);
}

void Apply() {
  State& state = GetState();
  {
    std::lock_guard lock(state.mutex);
    if (!state.started) return;
    state.pending = true;
  }
  state.wake.notify_all();
}

void Stop() {
  State& state = GetState();
  {
    std::lock_guard lock(state.mutex);
    if (!state.started) return;
    state.stop = true;
#ifdef _WIN32
    if (state.process) TerminateProcess(state.process, 1);
#endif
  }
  state.wake.notify_all();
  if (state.worker.joinable()) state.worker.join();
  std::lock_guard lock(state.mutex);
  state.started = false;
  state.shown.clear();
  state.shown_scale.clear();
}

std::string Note(Row row) {
  const char* name = row == Row::kTrack ? "fm4_track_textures" : row == Row::kRoad ? "fm4_road_surface" : "fm4_grass_texture";
  const Scale scale = Setting(name);
  if (row != Row::kTrack && REXCVAR_GET(fm4_track_signage) &&
      signage::ShowsPicture(row == Row::kRoad ? kRoadHash : kGrassHash)) {
    return "Your own picture is shown (TRACK SIGNAGE).";
  }
  if (scale == Scale::kDefault) return "";
  State& state = GetState();
  std::lock_guard lock(state.mutex);
  switch (state.phase) {
    case Phase::kDownloading:
      return "DOWNLOADING THE UPSCALER";
    case Phase::kUpscaling:
      return "UPSCALING " + ScaleLabel(state.working_scale) + ": " + std::to_string(state.done) + " OF " +
             std::to_string(state.total);
    case Phase::kFailed:
      return "COULD NOT UPSCALE: " + Upper(state.error) + ".";
    case Phase::kIdle:
      break;
  }
  if (row == Row::kTrack) {
    return "HOCKENHEIMRING AT " + ScaleLabel(scale) + ": " + std::to_string(state.shown_track) + " TEXTURES.";
  }
  return state.shown.contains(row == Row::kRoad ? kRoadHash : kGrassHash) ? "HOCKENHEIMRING AT " + ScaleLabel(scale) + "."
                                                                          : "";
}

std::shared_ptr<const RuntimeImage> ImageFor(uint64_t hash) {
  State& state = GetState();
  std::lock_guard lock(state.mutex);
  const auto found = state.shown.find(hash);
  return found != state.shown.end() ? found->second : nullptr;
}

}  // namespace fm4_static_recomp::track_textures
