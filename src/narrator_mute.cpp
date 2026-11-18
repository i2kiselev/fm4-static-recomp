// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "narrator_mute.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
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
  const uint32_t headers_end = kHeaderSize + ReadU32(bank, 8);
  if (count == 0 || headers_end > bank.size()) return false;
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

bool SetNarratorMuteLive(const std::filesystem::path& game_data_root, bool mute) {
#ifdef _WIN32
  uint8_t* const base = rex::system::kernel_state()->memory()->virtual_membase();
  // FMOD keeps the bank's sample headers, as the file has them and in a working copy with
  // their fields byte-swapped; it reads the length, loop and volume from them each time a
  // clip plays. Both kinds are rewritten, for the bank of whichever language is loaded.
  std::error_code ec;
  int copies = 0;
  for (const auto& entry :
       std::filesystem::directory_iterator(game_data_root / "Media" / "audio" / "VO", ec)) {
    const auto headers = ReadSampleHeaders(entry.path() / "vo.fsb");
    if (!headers || headers->original.empty()) continue;
    const auto& want = mute ? headers->muted : headers->original;
    const std::vector<uint8_t> raw_keys[2] = {
        std::vector<uint8_t>(headers->original[0].begin(), headers->original[0].begin() + 48),
        std::vector<uint8_t>(headers->muted[0].begin(), headers->muted[0].begin() + 48)};
    const std::vector<uint8_t> working_key = WorkingKey(headers->original[0]);
    // The guest's heaps, read through the host's map so that pages not in use are skipped.
    for (uint64_t guest = 0x40000000; guest < 0x80000000;) {
      MEMORY_BASIC_INFORMATION info{};
      if (!VirtualQuery(base + guest, &info, sizeof(info))) break;
      const auto region_end = std::min<uint64_t>(
          uint64_t(static_cast<uint8_t*>(info.BaseAddress) - base) + info.RegionSize, 0x80000000);
      if (info.State == MEM_COMMIT && (info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) {
        uint8_t* const begin = base + guest;
        uint8_t* const end = base + region_end;
        for (const auto& key : raw_keys) {
          for (uint8_t* at = std::search(begin, end, key.begin(), key.end()); at != end;
               at = std::search(at + 1, end, key.begin(), key.end())) {
            size_t offset = 0;
            for (const auto& header : want) {
              if (at + offset + header.size() > end) break;
              std::memcpy(at + offset, header.data(), header.size());
              offset += header.size();
            }
            ++copies;
          }
        }
        for (uint8_t* at = std::search(begin, end, working_key.begin(), working_key.end()); at != end;
             at = std::search(at + 1, end, working_key.begin(), working_key.end())) {
          uint8_t* record = at;
          size_t patched = 0;
          for (size_t i = 0; i < want.size(); ++i) {
            const auto& header = want[i];
            const auto key = WorkingKey(header);
            if (record + header.size() > end || !std::equal(key.begin(), key.end(), record)) break;
            // Every language names its clips alike: this language's bank only if the compressed
            // size (never changed by the mute) is its own.
            constexpr size_t kLengthCompressed = 36;
            const uint32_t compressed =
                uint32_t(record[kLengthCompressed]) << 24 | uint32_t(record[kLengthCompressed + 1]) << 16 |
                uint32_t(record[kLengthCompressed + 2]) << 8 | record[kLengthCompressed + 3];
            if (compressed != ReadU32(header, kLengthCompressed)) break;
            StoreBigEndian<uint32_t>(record + kLengthSamples, ReadU32(header, kLengthSamples));
            StoreBigEndian<uint32_t>(record + kLoopStart, ReadU32(header, kLoopStart));
            StoreBigEndian<uint32_t>(record + kLoopEnd, ReadU32(header, kLoopEnd));
            StoreBigEndian<uint16_t>(record + kDefaultVolume, ReadU16(header, kDefaultVolume));
            record += header.size();
            ++patched;
          }
          if (patched) ++copies;
        }
      }
      guest = region_end;
    }
  }
  REXLOG_INFO("MUTE NARRATOR: {} the loaded voice bank ({} copies)", mute ? "muted" : "restored", copies);
  return copies > 0;
#else
  (void)game_data_root;
  (void)mute;
  return false;
#endif
}

void InstallNarratorMute(const std::filesystem::path& game_data_root) {
  // Served muted from the start; SetNarratorMuteLive changes the loaded bank later.
  if (!REXCVAR_GET(fm4_mute_narrator)) return;
  // The game opens one language's bank; muting each one keeps fm4_language and the console
  // language out of it.
  std::error_code ec;
  int banks = 0;
  for (const auto& entry :
       std::filesystem::directory_iterator(game_data_root / "Media" / "audio" / "VO", ec)) {
    const auto path = entry.path() / "vo.fsb";
    if (!std::filesystem::is_regular_file(path, ec)) continue;
    const std::string language = entry.path().filename().string();
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
