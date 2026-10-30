#include "code_image_check.h"

#include <windows.h>

#include <bcrypt.h>

#include <array>
#include <cctype>
#include <fstream>
#include <vector>

namespace fm4_static_recomp {
namespace {

bool SameHex(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::toupper(static_cast<unsigned char>(a[i])) !=
        std::toupper(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

// "A or B" of the releases of one code image.
std::string ReleaseNames(std::span<const DiscRelease> releases, std::string_view image) {
  std::string names;
  for (const DiscRelease& release : releases) {
    if (release.code_image == image) {
      if (!names.empty()) {
        names += " or ";
      }
      names += release.name;
    }
  }
  return names;
}

}  // namespace

std::string FileSha256(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (!BCRYPT_SUCCESS(
          BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash, nullptr, 0, nullptr, 0, 0))) {
    return {};
  }
  std::vector<char> buffer(1 << 20);
  bool ok = true;
  while (ok && file) {
    file.read(buffer.data(), std::streamsize(buffer.size()));
    const auto read = static_cast<ULONG>(file.gcount());
    ok = read == 0 ||
         BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), read, 0));
  }
  std::array<UCHAR, 32> digest{};
  ok = ok && file.eof() &&
       BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), ULONG(digest.size()), 0));
  BCryptDestroyHash(hash);
  if (!ok) {
    return {};
  }
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string hex;
  for (const UCHAR byte : digest) {
    hex += kHex[byte >> 4];
    hex += kHex[byte & 15];
  }
  return hex;
}

std::optional<std::string> CodeImageMismatch(std::string_view executable_sha256,
                                             std::string_view built_image,
                                             std::span<const DiscRelease> releases) {
  if (executable_sha256.empty()) {
    return "default.xex in the game folder cannot be read.";
  }
  for (const DiscRelease& release : releases) {
    if (!SameHex(release.executable_sha256, executable_sha256)) {
      continue;
    }
    if (release.code_image == built_image) {
      return std::nullopt;
    }
    return "This build of FM4 Static Recomp was made from the " +
           ReleaseNames(releases, built_image) + " disc, but the game folder holds " +
           std::string(release.name) +
           ", whose game code differs. Run setup again with the disc in the game folder.";
  }
  return "default.xex in the game folder (SHA-256 " + std::string(executable_sha256) +
         ") is from no supported Disc 1 release. Run setup again with a supported disc.";
}

}  // namespace fm4_static_recomp
