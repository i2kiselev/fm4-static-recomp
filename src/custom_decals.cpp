// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "custom_decals.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>

namespace fm4_static_recomp::decals {
namespace {

constexpr int kTiledDownTo = 32;         // levels at least this size are tiled
constexpr size_t kTrailerSize = 180;     // after the last level: the quad's trailer
constexpr size_t kDecalWidthAt = 0x13F;  // the texture header: width, height, levels (big-endian)
constexpr uint16_t kFirstKey = 0xF4A0;   // name keys are looked for from here up

// Sort rows (ALL SPONSOR DECALS): ids from kSortIdBase, by maker; the listless game decals after a
// maker's usual ones (whose sequences are below 100).
constexpr int64_t kSortIdBase = 90000000;
constexpr int kSponsorSequence = 900;

std::atomic<bool> g_custom_picker{false};

int Levels(int size) {  // size .. 1
  int n = 0;
  for (int w = size; w >= 1; w /= 2) ++n;
  return n;
}

uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
void PutBe32(std::vector<uint8_t>& v, uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s));
}

// Byte offset of pixel (x, y) in an Xbox 360 tiled w x w level of 32-bit pixels.
size_t TiledOffset(int x, int y, int w) {
  constexpr int lb = 2;
  const int aw = (w + 31) & ~31;
  const int64_t X = x, Y = y;
  const int64_t macro = ((X >> 5) + (Y >> 5) * (aw >> 5)) << (lb + 7);
  const int64_t micro = ((X & 7) + ((Y & 6) << 2)) << lb;
  const int64_t off = macro + ((micro & ~15) << 1) + (micro & 15) + ((Y & 8) << (3 + lb)) + ((Y & 1) << 4);
  return size_t(((off & ~511) << 3) + ((off & 448) << 2) + (off & 63) + ((Y & 16) << 7) +
                (((((Y & 8) >> 2) + (X >> 3)) & 3) << 6));
}

// Half size, averaging each 2 x 2; colours weighted by alpha so transparent pixels add no fringe.
std::vector<uint8_t> HalfSize(const std::vector<uint8_t>& rgba, int w) {
  const int h = w / 2;
  std::vector<uint8_t> out(size_t(h) * h * 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < h; ++x) {
      double a = 0, c[3] = {0, 0, 0}, plain[3] = {0, 0, 0};
      for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
          const uint8_t* p = &rgba[(size_t(2 * y + dy) * w + (2 * x + dx)) * 4];
          a += p[3];
          for (int k = 0; k < 3; ++k) {
            c[k] += double(p[k]) * p[3];
            plain[k] += p[k];
          }
        }
      }
      uint8_t* o = &out[(size_t(y) * h + x) * 4];
      for (int k = 0; k < 3; ++k) {
        o[k] = uint8_t(std::lround(a > 0 ? c[k] / a : plain[k] / 4));
      }
      o[3] = uint8_t(std::lround(a / 4));
    }
  }
  return out;
}

// An SQL string literal.
std::string Quoted(std::string_view text) {
  std::string out = "'";
  for (char c : text) {
    out += c;
    if (c == '\'') out += '\'';
  }
  return out + "'";
}

// The published state. Name lookups are frequent (every text the game shows), so a key is
// checked against `used` before the lock is taken.
struct State {
  std::mutex mutex;
  uint64_t generation = 0;
  uint64_t taken = 0;  // the generation the game last took
  std::vector<std::string> statements;
  std::map<uint16_t, std::u16string> names;
  Status status;
  std::array<std::atomic<bool>, 0x10000> used{};
  std::function<void()> rebuild;
  Settings settings;
  uint64_t failed_generation = 0;
  int failed = 0;
};

// A retired decal's place in the list; the custom picker leaves these out.
constexpr std::string_view kRetiredSort = "~retired~";

State& state() {
  static State s;
  return s;
}

}  // namespace

bool IsDecalFile(const std::vector<uint8_t>& file) {
  if (file.size() != kDecalFileSize) return false;
  if (Be32(&file[kDecalWidthAt]) != kGameDecalSize || Be32(&file[kDecalWidthAt + 4]) != kGameDecalSize ||
      Be32(&file[kDecalWidthAt + 8]) != uint32_t(Levels(kGameDecalSize))) {
    return false;
  }
  static constexpr uint8_t kMarker[] = {0, 0, 0, 1};
  if (std::memcmp(&file[kDecalPixelsAt - 4], kMarker, 4) != 0) return false;
  static constexpr std::string_view kMaterial = "01___Default";
  const auto tail = std::string_view(reinterpret_cast<const char*>(file.data()) + file.size() - 200, 200);
  return tail.find(kMaterial) != std::string_view::npos;
}

size_t DecalFileSize(int size) {
  size_t pixels = 0;
  for (int w = size; w >= 1; w /= 2) pixels += size_t(w) * w * 4;
  return kDecalPixelsAt + pixels + 4 * size_t(Levels(size) - 1) + kTrailerSize;
}

std::vector<uint8_t> EncodeDecal(const std::vector<uint8_t>& rgba, int size, const std::vector<uint8_t>& templ) {
  // The template's mesh and texture header (its size and level count set to this size), the
  // levels, the template's trailer.
  std::vector<uint8_t> out(templ.begin(), templ.begin() + kDecalPixelsAt);
  for (int k = 0; k < 3; ++k) {
    const uint32_t v = k < 2 ? uint32_t(size) : uint32_t(Levels(size));
    for (int b = 0; b < 4; ++b) out[kDecalWidthAt + 4 * k + b] = uint8_t(v >> (24 - 8 * b));
  }
  out.reserve(DecalFileSize(size));
  std::vector<uint8_t> level = rgba;
  for (int w = size; w >= 1; w /= 2) {
    if (w < size) {
      level = HalfSize(level, w * 2);
      PutBe32(out, 1);  // before each level after the first
    }
    const size_t at = out.size();
    out.resize(at + size_t(w) * w * 4);
    for (int y = 0; y < w; ++y) {
      for (int x = 0; x < w; ++x) {
        const uint8_t* p = &level[(size_t(y) * w + x) * 4];
        uint8_t* o = &out[at + (w >= kTiledDownTo ? TiledOffset(x, y, w) : (size_t(y) * w + x) * 4)];
        o[0] = p[3];  // ARGB
        o[1] = p[0];
        o[2] = p[1];
        o[3] = p[2];
      }
    }
  }
  out.insert(out.end(), templ.end() - kTrailerSize, templ.end());
  return out;
}

std::optional<std::vector<uint8_t>> DecodeDecal(const std::vector<uint8_t>& file, int* size_out) {
  if (file.size() < kDecalPixelsAt + kTrailerSize) return std::nullopt;
  const uint32_t size = Be32(&file[kDecalWidthAt]);
  if (size < kTiledDownTo || size > 4096 || (size & (size - 1)) || Be32(&file[kDecalWidthAt + 4]) != size ||
      file.size() != DecalFileSize(int(size))) {
    return std::nullopt;
  }
  std::vector<uint8_t> rgba(size_t(size) * size * 4);
  for (uint32_t y = 0; y < size; ++y) {
    for (uint32_t x = 0; x < size; ++x) {
      const uint8_t* p = &file[kDecalPixelsAt + TiledOffset(int(x), int(y), int(size))];
      uint8_t* o = &rgba[(size_t(y) * size + x) * 4];
      o[0] = p[1];
      o[1] = p[2];
      o[2] = p[3];
      o[3] = p[0];
    }
  }
  if (size_out) *size_out = int(size);
  return rgba;
}

std::vector<uint8_t> FitImage(const std::vector<uint8_t>& rgba, int w, int h, int size) {
  std::vector<uint8_t> out(size_t(size) * size * 4, 0);
  if (w <= 0 || h <= 0 || rgba.size() < size_t(w) * h * 4) return out;
  const double scale = std::min(double(size) / w, double(size) / h);
  const int tw = std::max(1, int(std::lround(w * scale))), th = std::max(1, int(std::lround(h * scale)));
  const int ox = (size - tw) / 2, oy = (size - th) / 2;
  // Colours are averaged weighted by alpha, so transparent pixels add no dark fringe.
  struct Sum {
    double a = 0, c[3] = {0, 0, 0}, plain[3] = {0, 0, 0}, n = 0;
    void Add(const uint8_t* p, double weight) {
      a += p[3] * weight;
      for (int k = 0; k < 3; ++k) {
        c[k] += double(p[k]) * p[3] * weight;
        plain[k] += p[k] * weight;
      }
      n += weight;
    }
  };
  const auto pixel = [&](int x, int y) {
    return &rgba[(size_t(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)) * 4];
  };
  for (int y = 0; y < th; ++y) {
    for (int x = 0; x < tw; ++x) {
      Sum sum;
      if (scale >= 1) {
        // Enlarging: bilinear between the four nearest source pixels.
        const double sx = (x + 0.5) / scale - 0.5, sy = (y + 0.5) / scale - 0.5;
        const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
        const double fx = sx - x0, fy = sy - y0;
        sum.Add(pixel(x0, y0), (1 - fx) * (1 - fy));
        sum.Add(pixel(x0 + 1, y0), fx * (1 - fy));
        sum.Add(pixel(x0, y0 + 1), (1 - fx) * fy);
        sum.Add(pixel(x0 + 1, y0 + 1), fx * fy);
      } else {
        // Shrinking: the average of the source area the pixel covers, at 4 x 4 samples.
        constexpr int kSamples = 4;
        for (int sy = 0; sy < kSamples; ++sy) {
          for (int sx = 0; sx < kSamples; ++sx) {
            sum.Add(pixel(int((x + (sx + 0.5) / kSamples) / scale), int((y + (sy + 0.5) / kSamples) / scale)), 1);
          }
        }
      }
      uint8_t* o = &out[(size_t(oy + y) * size + (ox + x)) * 4];
      for (int k = 0; k < 3; ++k) {
        const double v = sum.a > 0 ? sum.c[k] / sum.a : sum.plain[k] / sum.n;
        o[k] = uint8_t(std::clamp(std::lround(v), 0L, 255L));
      }
      o[3] = uint8_t(std::clamp(std::lround(sum.a / sum.n), 0L, 255L));
    }
  }
  return out;
}

std::optional<std::set<uint16_t>> StringHashes(const std::vector<uint8_t>& table) {
  // LSB2: a header of 0x0C bytes, section offsets; the first section is the keys (16-bit, each
  // with a text offset) after its size and count.
  if (table.size() < 0x24 || std::memcmp(table.data(), "LSB2", 4) != 0) return std::nullopt;
  const size_t keys = Be32(&table[0x14]);
  if (keys + 8 > table.size()) return std::nullopt;
  const uint32_t count = Be32(&table[keys + 4]);
  if (keys + 8 + size_t(count) * 6 > table.size()) return std::nullopt;
  std::set<uint16_t> out;
  for (uint32_t i = 0; i < count; ++i) out.insert(Be16(&table[keys + 8 + size_t(i) * 6]));
  return out;
}

std::map<std::string, int> AssignIds(const std::vector<std::string>& names, std::map<std::string, int> ids) {
  std::set<int> used;
  for (const auto& [n, id] : ids) used.insert(id);
  int next = kFirstDecalId;
  for (const std::string& name : names) {
    if (ids.contains(name)) continue;
    while (used.contains(next)) ++next;
    if (next >= kFirstDecalId + kMaxDecals) break;  // full: the rest are left out
    ids[name] = next;
    used.insert(next);
  }
  return ids;
}

std::map<std::string, int> ParseIds(std::string_view text) {
  std::map<std::string, int> ids;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    start = end + 1;
    const size_t tab = line.find('\t');
    if (tab == std::string_view::npos) continue;
    int id = 0;
    const auto r = std::from_chars(line.data(), line.data() + tab, id);
    if (r.ec != std::errc() || r.ptr != line.data() + tab) continue;
    if (id < kFirstDecalId || id >= kFirstDecalId + kMaxDecals) continue;
    ids[std::string(line.substr(tab + 1))] = id;
  }
  return ids;
}

std::string FormatIds(const std::map<std::string, int>& ids) {
  std::vector<std::pair<int, std::string>> rows;
  for (const auto& [n, id] : ids) rows.emplace_back(id, n);
  std::sort(rows.begin(), rows.end());
  std::string out = "# CUSTOM DECALS: each image keeps its decal id (a car's livery remembers its decals by id)\n";
  for (const auto& [id, n] : rows) out += std::to_string(id) + "\t" + n + "\n";
  return out;
}

std::map<int, uint16_t> AssignKeys(const std::vector<int>& ids, const std::set<uint16_t>& taken) {
  std::vector<int> sorted = ids;
  std::sort(sorted.begin(), sorted.end());
  std::map<int, uint16_t> out;
  uint32_t key = kFirstKey;
  for (int id : sorted) {
    while (key <= 0xFFFE && taken.contains(uint16_t(key))) ++key;
    if (key > 0xFFFE) break;
    out[id] = uint16_t(key++);
  }
  return out;
}

std::string DecalFileName(int id, uint64_t image_hash) {
  char name[64];
  std::snprintf(name, sizeof(name), "custom_%d_%016llx.carbin", id, static_cast<unsigned long long>(image_hash));
  return name;
}

uint64_t ImageHash(const std::vector<uint8_t>& bytes) {
  uint64_t hash = 0xCBF29CE484222325ull;  // FNV-1a
  for (uint8_t b : bytes) {
    hash ^= b;
    hash *= 0x100000001B3ull;
  }
  return hash;
}

std::vector<std::string> DecalStatements(const std::vector<LiveDecal>& decals, bool all_sponsors) {
  const std::string first = std::to_string(kFirstDecalId), last = std::to_string(kFirstDecalId + kMaxDecals - 1);
  const std::string sort_base = std::to_string(kSortIdBase);
  // A maker's rows: its id * 2000 + 1000 + the game decal's id.
  const std::string makers = "(SELECT DISTINCT MakeID FROM Livery_DecalsSortOrder WHERE ID < " + sort_base + ") m";
  std::vector<std::string> out = {
      "DELETE FROM Livery_DecalsSortOrder WHERE ID >= " + sort_base,
      "DELETE FROM Livery_Decals WHERE ID BETWEEN " + first + " AND " + last,
  };
  for (const LiveDecal& d : decals) {
    const uint32_t ref = kDecalStringTable << 16 | d.key;
    out.push_back("INSERT INTO Livery_Decals (ID, Filename, DisplayName, AlphaSort) VALUES (" + std::to_string(d.id) +
                  ", " + Quoted("GAME:\\Media\\Livery\\Decals\\" + d.file) + ", " +
                  Quoted("_&" + std::to_string(ref)) + ", " +
                  Quoted(d.retired ? std::string(kRetiredSort) + d.name : d.name) + ")");
  }
  if (all_sponsors) {
    out.push_back("INSERT INTO Livery_DecalsSortOrder (ID, MakeID, Sequence, Livery_DecalID) SELECT " + sort_base +
                  " + m.MakeID * 2000 + 1000 + d.ID, m.MakeID, " + std::to_string(kSponsorSequence) + ", d.ID FROM " +
                  makers + ", Livery_Decals d WHERE d.ID < " + first +
                  " AND d.ID NOT IN (SELECT Livery_DecalID FROM Livery_DecalsSortOrder WHERE ID < " + sort_base + ")");
  }
  return out;
}

void SetCustomPicker(bool custom) { g_custom_picker = custom; }

bool CustomPicker() { return g_custom_picker; }

std::optional<std::string> CustomPickerQuery(std::string_view sql) {
  static constexpr std::string_view kList =
      " INNER JOIN Livery_DecalsSortOrder ON (Livery_Decals.ID = Livery_DecalsSortOrder.Livery_DecalID)";
  if (!sql.starts_with("SELECT ")) return std::nullopt;
  const size_t at = sql.find(kList);
  if (at == std::string_view::npos) return std::nullopt;
  return std::string(sql.substr(0, at)) + " WHERE Livery_Decals.ID BETWEEN " + std::to_string(kFirstDecalId) +
         " AND " + std::to_string(kFirstDecalId + kMaxDecals - 1) + " AND Livery_Decals.AlphaSort NOT LIKE '" +
         std::string(kRetiredSort) + "%' ORDER BY Livery_Decals.AlphaSort";
}

void Publish(std::vector<std::string> statements, std::map<uint16_t, std::u16string> names, Status status) {
  State& s = state();
  std::lock_guard lock(s.mutex);
  for (const auto& [key, name] : s.names) s.used[key] = false;
  for (const auto& [key, name] : names) s.used[key] = true;
  s.statements = std::move(statements);
  s.names = std::move(names);
  s.status = std::move(status);
  ++s.generation;
}

void SetBuilding() {
  State& s = state();
  std::lock_guard lock(s.mutex);
  s.status.building = true;
}

Status CurrentStatus() {
  State& s = state();
  std::lock_guard lock(s.mutex);
  return s.status;
}

bool StatementsSince(uint64_t& generation, std::vector<std::string>* statements) {
  State& s = state();
  std::lock_guard lock(s.mutex);
  if (s.generation == generation) return false;
  generation = s.taken = s.generation;
  if (statements) *statements = s.statements;
  return true;
}

void StatementsFailed(uint64_t& generation) {
  constexpr int kTries = 3;
  State& s = state();
  std::lock_guard lock(s.mutex);
  if (s.failed_generation != generation) {
    s.failed_generation = generation;
    s.failed = 0;
  }
  if (++s.failed < kTries) {
    generation = 0;  // taken again at the next look (the statements start by clearing the custom rows)
    s.taken = 0;
  } else {
    s.status.problems.push_back("Some custom decals could not be added to the game's database.");
  }
}

void SetSettings(Settings settings) {
  State& s = state();
  std::lock_guard lock(s.mutex);
  s.settings = std::move(settings);
}

Settings CurrentSettings() {
  State& s = state();
  std::lock_guard lock(s.mutex);
  return s.settings;
}

bool ChangesWaiting() {
  State& s = state();
  std::lock_guard lock(s.mutex);
  return s.status.building || s.generation != s.taken;
}

void SetRebuild(std::function<void()> rebuild) {
  State& s = state();
  std::lock_guard lock(s.mutex);
  s.rebuild = std::move(rebuild);
}

void ApplyCustomDecals() {
  State& s = state();
  std::function<void()> rebuild;
  {
    std::lock_guard lock(s.mutex);
    rebuild = s.rebuild;
  }
  if (rebuild) rebuild();
}

std::optional<std::u16string> DecalName(uint16_t key) {
  State& s = state();
  if (!s.used[key]) return std::nullopt;
  std::lock_guard lock(s.mutex);
  const auto found = s.names.find(key);
  if (found == s.names.end()) return std::nullopt;
  const bool label = key == s.status.apply_key || key == s.status.change_key;
  if (label && (s.status.building || s.generation != s.taken)) {
    return found->second + std::u16string(kReopenNote);
  }
  return found->second;
}

}  // namespace fm4_static_recomp::decals
