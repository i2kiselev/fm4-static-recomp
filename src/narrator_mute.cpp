// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "narrator_mute.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem/content_override.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/system/kernel_state.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

REXCVAR_DECLARE(std::string, fm4_language);

REXCVAR_DEFINE_BOOL(fm4_mute_narrator, false, "Forza Motorsport 4",
                    "Silences the narrator who introduces World Tour events and tracks in the menus; "
                    "Jeremy Clarkson's Autovista commentary stays. PC OPTIONS > GAMEPLAY switches it at once");

namespace fm4_static_recomp {
namespace {

// FSB4: "FSB4", sample count, sample header bytes, data bytes, version, mode, then the
// 16-byte hash and GUID, so the sample headers start at 0x30. Each sample header begins with
// its own size; the fields used here are at fixed offsets from it.
constexpr size_t kHeaderSize = 0x30;
constexpr size_t kLengthSamples = 32;
constexpr size_t kLoopStart = 40;
constexpr size_t kLoopEnd = 44;
constexpr size_t kDefaultVolume = 56;
constexpr size_t kMinSampleHeader = kDefaultVolume + 2;
constexpr uint16_t kFullVolume = 255;
constexpr uint32_t kXmaFrameSamples = 512;

uint32_t ReadU32(std::span<const uint8_t> bytes, size_t at) {
  uint32_t value;
  std::memcpy(&value, bytes.data() + at, sizeof(value));
  return value;
}

uint16_t ReadU16(std::span<const uint8_t> bytes, size_t at) {
  uint16_t value;
  std::memcpy(&value, bytes.data() + at, sizeof(value));
  return value;
}

template <typename T>
void Write(std::span<uint8_t> bytes, size_t at, T value) {
  std::memcpy(bytes.data() + at, &value, sizeof(value));
}

}  // namespace

bool MuteNarratorBank(std::span<uint8_t> bank) {
  if (bank.size() < kHeaderSize || std::memcmp(bank.data(), "FSB4", 4) != 0) return false;
  const uint32_t count = ReadU32(bank, 4);
  const uint64_t headers_end = uint64_t(kHeaderSize) + ReadU32(bank, 8);
  if (count == 0 || headers_end > bank.size()) return false;
  if (count > (headers_end - kHeaderSize) / kMinSampleHeader) return false;
  std::vector<size_t> samples;
  samples.reserve(count);
  size_t at = kHeaderSize;
  for (uint32_t i = 0; i < count; ++i) {
    if (at + 2 > headers_end) return false;
    const uint16_t size = ReadU16(bank, at);
    if (size < kMinSampleHeader || at + size > headers_end) return false;
    if (ReadU16(bank, at + kDefaultVolume) != kFullVolume) return false;
    samples.push_back(at);
    at += size;
  }
  for (const size_t sample : samples) {
    Write<uint16_t>(bank, sample + kDefaultVolume, 0);
    Write<uint32_t>(bank, sample + kLengthSamples, kXmaFrameSamples);
    Write<uint32_t>(bank, sample + kLoopStart, 0);
    Write<uint32_t>(bank, sample + kLoopEnd, kXmaFrameSamples - 1);
  }
  return true;
}

namespace {

// A bank's sample headers as the disc has them, each muted or not.
struct SampleHeaders {
  std::vector<std::vector<uint8_t>> original;
  std::vector<std::vector<uint8_t>> muted;
};

std::optional<SampleHeaders> ReadSampleHeaders(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::vector<uint8_t> bank((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::vector<uint8_t> muted = bank;
  if (!MuteNarratorBank(muted)) return std::nullopt;
  SampleHeaders headers;
  const uint32_t count = ReadU32(bank, 4);
  size_t at = kHeaderSize;
  for (uint32_t i = 0; i < count; ++i) {
    const uint16_t size = ReadU16(bank, at);
    headers.original.emplace_back(bank.begin() + at, bank.begin() + at + size);
    headers.muted.emplace_back(muted.begin() + at, muted.begin() + at + size);
    at += size;
  }
  return headers;
}

// The first bytes of a sample header in FMOD's working copy, which has every field
// byte-swapped on its own: the size (big-endian) and the name as it is.
std::vector<uint8_t> WorkingKey(const std::vector<uint8_t>& header) {
  std::vector<uint8_t> key(header.begin(), header.begin() + 32);
  std::swap(key[0], key[1]);
  return key;
}

template <typename T>
void StoreBigEndian(uint8_t* at, T value) {
  for (size_t i = 0; i < sizeof(T); ++i) at[i] = uint8_t(value >> (8 * (sizeof(T) - 1 - i)));
}

}  // namespace

namespace {

// A language's bank: its folder and its sample headers as the disc has them, each muted or not.
struct LanguageBank {
  std::string language;
  std::filesystem::path path;
  SampleHeaders headers;
  size_t table_bytes = 0;
};

// The language folders under VO. The iterator's own steps take the error code: a folder going away ends the walk.
std::vector<std::filesystem::path> LanguageFolders(const std::filesystem::path& game_data_root) {
  std::vector<std::filesystem::path> folders;
  std::error_code ec;
  std::filesystem::directory_iterator it(game_data_root / "Media" / "audio" / "VO", ec);
  for (const std::filesystem::directory_iterator end; !ec && it != end; it.increment(ec)) {
    folders.push_back(it->path());
  }
  return folders;
}

#ifdef _WIN32
// Guest memory can be freed under this thread: every read and write goes through the calls that fail on a page
// that has gone instead of faulting.
bool SafeRead(const uint8_t* from, uint8_t* to, size_t size) {
  SIZE_T done = 0;
  return ReadProcessMemory(GetCurrentProcess(), from, to, size, &done) && done == size;
}

bool SafeWrite(uint8_t* to, const void* from, size_t size) {
  SIZE_T done = 0;
  return WriteProcessMemory(GetCurrentProcess(), to, from, size, &done) && done == size;
}

template <typename T>
bool SafeWriteBigEndian(uint8_t* to, T value) {
  uint8_t bytes[sizeof(T)];
  StoreBigEndian<T>(bytes, value);
  return SafeWrite(to, bytes, sizeof(T));
}

// Whether the whole table sits at `at`, each header as the disc has it or muted.
bool RawTableAt(const uint8_t* at, const uint8_t* end, const SampleHeaders& headers) {
  const uint8_t* record = at;
  for (size_t i = 0; i < headers.original.size(); ++i) {
    const auto& original = headers.original[i];
    if (size_t(end - record) < original.size()) return false;
    if (!std::equal(original.begin(), original.end(), record) &&
        !std::equal(headers.muted[i].begin(), headers.muted[i].end(), record)) {
      return false;
    }
    record += original.size();
  }
  return true;
}

// Whether FMOD's working copy of the whole table sits at `at`: names alike in every language, so each record's
// compressed size (never changed by the mute) must be this language's too.
bool WorkingTableAt(const uint8_t* at, const uint8_t* end, const SampleHeaders& headers) {
  constexpr size_t kLengthCompressed = 36;
  const uint8_t* record = at;
  for (const auto& header : headers.original) {
    if (size_t(end - record) < header.size()) return false;
    const auto key = WorkingKey(header);
    if (!std::equal(key.begin(), key.end(), record)) return false;
    const uint32_t compressed = uint32_t(record[kLengthCompressed]) << 24 |
                                uint32_t(record[kLengthCompressed + 1]) << 16 |
                                uint32_t(record[kLengthCompressed + 2]) << 8 | record[kLengthCompressed + 3];
    if (compressed != ReadU32(header, kLengthCompressed)) return false;
    record += header.size();
  }
  return true;
}

bool WriteRawTable(uint8_t* to, const std::vector<std::vector<uint8_t>>& want) {
  size_t offset = 0;
  for (const auto& header : want) {
    if (!SafeWrite(to + offset, header.data(), header.size())) return false;
    offset += header.size();
  }
  return true;
}

bool WriteWorkingTable(uint8_t* to, const std::vector<std::vector<uint8_t>>& want) {
  uint8_t* record = to;
  for (const auto& header : want) {
    if (!SafeWriteBigEndian<uint32_t>(record + kLengthSamples, ReadU32(header, kLengthSamples)) ||
        !SafeWriteBigEndian<uint32_t>(record + kLoopStart, ReadU32(header, kLoopStart)) ||
        !SafeWriteBigEndian<uint32_t>(record + kLoopEnd, ReadU32(header, kLoopEnd)) ||
        !SafeWriteBigEndian<uint16_t>(record + kDefaultVolume, ReadU16(header, kDefaultVolume))) {
      return false;
    }
    record += header.size();
  }
  return true;
}

// Rewrites this language's loaded tables in one guest range; the number found. `stale`: a newer choice is waiting.
template <typename Stale>
int PatchRange(uint8_t* base, uint64_t first, uint64_t last, const LanguageBank& bank, bool mute, Stale stale) {
  constexpr size_t kChunk = 8u << 20;
  const SampleHeaders& headers = bank.headers;
  const auto& want = mute ? headers.muted : headers.original;
  const std::vector<uint8_t> raw_keys[2] = {
      std::vector<uint8_t>(headers.original[0].begin(), headers.original[0].begin() + 48),
      std::vector<uint8_t>(headers.muted[0].begin(), headers.muted[0].begin() + 48)};
  const std::vector<uint8_t> working_key = WorkingKey(headers.original[0]);
  std::vector<uint8_t> buffer(kChunk + bank.table_bytes);
  int copies = 0;
  for (uint64_t guest = first; guest < last && !stale();) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(base + guest, &info, sizeof(info))) break;
    const uint64_t region_end =
        std::min<uint64_t>(uint64_t(static_cast<uint8_t*>(info.BaseAddress) - base) + info.RegionSize, last);
    if (info.State == MEM_COMMIT && (info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) {
      // In chunks that overlap by a table, so a table across a chunk's end is seen whole in the chunk it starts in.
      for (uint64_t at = guest; at < region_end && !stale(); at += kChunk) {
        const size_t size = size_t(std::min<uint64_t>(region_end - at, buffer.size()));
        if (!SafeRead(base + at, buffer.data(), size)) continue;
        const uint8_t* const begin = buffer.data();
        const uint8_t* const end = begin + size;
        const uint8_t* const starts_end = begin + std::min(size, kChunk);
        for (const auto& key : raw_keys) {
          for (const uint8_t* hit = std::search(begin, end, key.begin(), key.end());
               hit != end && hit < starts_end; hit = std::search(hit + 1, end, key.begin(), key.end())) {
            if (RawTableAt(hit, end, headers) && WriteRawTable(base + at + size_t(hit - begin), want)) ++copies;
          }
        }
        for (const uint8_t* hit = std::search(begin, end, working_key.begin(), working_key.end());
             hit != end && hit < starts_end;
             hit = std::search(hit + 1, end, working_key.begin(), working_key.end())) {
          if (WorkingTableAt(hit, end, headers) && WriteWorkingTable(base + at + size_t(hit - begin), want)) ++copies;
        }
      }
    }
    guest = region_end;
  }
  return copies;
}
#endif

}  // namespace

namespace {

// Mutes or restores the bank the game has loaded, in the guest's memory: FMOD keeps its sample headers as the file
// has them and in a working copy with their fields byte-swapped, and reads the length, loop and volume from them
// each time a clip plays. The first language found loaded is the one (the game loads one).
template <typename Stale>
bool SetNarratorMuteLive(const std::vector<LanguageBank>& banks, bool mute, Stale stale) {
#ifdef _WIN32
  auto* const state = rex::system::kernel_state();
  if (!state || !state->memory()) return false;
  uint8_t* const base = state->memory()->virtual_membase();
  // The virtual heaps and the physical one (its first view: the others alias it), where FMOD's pools can sit.
  static constexpr std::pair<uint64_t, uint64_t> kRanges[] = {{0x00010000, 0x80000000}, {0xA0000000, 0xC0000000}};
  for (const LanguageBank& bank : banks) {
    int copies = 0;
    for (const auto& [first, last] : kRanges) copies += PatchRange(base, first, last, bank, mute, stale);
    if (stale()) return false;
    if (copies) {
      REXLOG_INFO("MUTE NARRATOR: {} the loaded {} voice bank ({} copies)", mute ? "muted" : "restored", bank.language,
                  copies);
      return true;
    }
  }
  REXLOG_WARN("MUTE NARRATOR: the loaded voice bank was not found: it changes from the next start");
#else
  (void)banks;
  (void)mute;
  (void)stale;
#endif
  return false;
}

}  // namespace

namespace {

// One thread does the changes, taking only the newest choice; it stops, and is joined, as the game shuts down.
struct LiveMute {
  std::mutex mutex;
  std::condition_variable wake;
  std::thread thread;
  std::optional<bool> wanted;
  std::filesystem::path root;
  std::atomic<uint64_t> generation{0};
  bool stop = false;
  std::vector<LanguageBank> banks;  // read once, on this thread
};

LiveMute& Live() {
  static LiveMute live;
  return live;
}

std::string Lower(std::string text) {
  std::ranges::transform(text, text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}

// The sample headers of every language, the game's own language first (it is the one loaded).
void LoadBanks(LiveMute& live, const std::filesystem::path& root) {
  if (!live.banks.empty()) return;
  for (const auto& folder : LanguageFolders(root)) {
    auto headers = ReadSampleHeaders(folder / "vo.fsb");
    if (!headers || headers->original.empty()) continue;
    LanguageBank bank{folder.filename().string(), folder / "vo.fsb", std::move(*headers)};
    for (const auto& header : bank.headers.original) bank.table_bytes += header.size();
    live.banks.push_back(std::move(bank));
  }
  const std::string code = Lower(REXCVAR_GET(fm4_language));
  std::ranges::stable_partition(live.banks, [&](const LanguageBank& bank) {
    return !code.empty() && Lower(bank.language) == code;
  });
}

// What the game reads next time it opens a bank follows the choice as well.
void SetOverrides(const LiveMute& live, bool mute) {
  for (const LanguageBank& bank : live.banks) {
    const std::string guest_path = "game:\\media\\audio\\vo\\" + bank.language + "\\vo.fsb";
    if (!mute) {
      rex::filesystem::SetContentOverride(guest_path, {});
      continue;
    }
    std::ifstream file(bank.path, std::ios::binary);
    std::vector<uint8_t> content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (MuteNarratorBank(content)) rex::filesystem::SetContentOverride(guest_path, std::move(content));
  }
}

void LiveThread() {
  LiveMute& live = Live();
  for (;;) {
    bool mute = false;
    std::filesystem::path root;
    uint64_t generation = 0;
    {
      std::unique_lock lock(live.mutex);
      live.wake.wait(lock, [&] { return live.stop || live.wanted.has_value(); });
      if (live.stop) return;
      mute = *live.wanted;
      live.wanted.reset();
      root = live.root;
      generation = live.generation.load();
    }
    const auto stale = [&] { return live.generation.load() != generation; };
    LoadBanks(live, root);
    if (!stale()) SetOverrides(live, mute);
    SetNarratorMuteLive(live.banks, mute, stale);
  }
}

}  // namespace

void RequestNarratorMute(const std::filesystem::path& game_data_root, bool mute) {
  LiveMute& live = Live();
  std::lock_guard lock(live.mutex);
  if (live.stop) return;
  live.root = game_data_root;
  live.wanted = mute;
  live.generation.fetch_add(1);
  if (!live.thread.joinable()) live.thread = std::thread(LiveThread);
  live.wake.notify_one();
}

void StopNarratorMute() {
  LiveMute& live = Live();
  {
    std::lock_guard lock(live.mutex);
    live.stop = true;
    live.generation.fetch_add(1);  // a search going on ends at its next step
  }
  live.wake.notify_one();
  if (live.thread.joinable()) live.thread.join();
}

void InstallNarratorMute(const std::filesystem::path& game_data_root) {
  // Served muted from the start; RequestNarratorMute changes the loaded bank later.
  if (!REXCVAR_GET(fm4_mute_narrator)) return;
  // The game opens one language's bank; muting each one keeps fm4_language and the console
  // language out of it.
  std::error_code ec;
  int banks = 0;
  for (const auto& folder : LanguageFolders(game_data_root)) {
    const auto path = folder / "vo.fsb";
    if (!std::filesystem::is_regular_file(path, ec)) continue;
    const std::string language = folder.filename().string();
    const std::string guest_path = "game:\\media\\audio\\vo\\" + language + "\\vo.fsb";
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> bank((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!MuteNarratorBank(bank)) {
      REXLOG_WARN("MUTE NARRATOR not applied to {}: the voice bank is not the disc's", language);
      continue;
    }
    rex::filesystem::SetContentOverride(guest_path, std::move(bank));
    ++banks;
  }
  if (banks == 0) {
    REXLOG_WARN("MUTE NARRATOR: no voice bank found under {}", game_data_root.string());
    return;
  }
  REXLOG_INFO("MUTE NARRATOR: {} voice banks muted", banks);
}

}  // namespace fm4_static_recomp
