#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fm4_static_recomp {

// A supported Disc 1 release (config/supported-discs.json). Releases of one code image hold
// byte-identical decrypted code; the build is recompiled from one image's code and runs only
// those releases.
struct DiscRelease {
  std::string_view name;
  std::string_view code_image;
  std::string_view executable_sha256;  // default.xex, upper-case hex
};

// The SHA-256 of a file as upper-case hex; empty when it cannot be read.
std::string FileSha256(const std::filesystem::path& path);

// Why this build cannot run a game folder whose default.xex has this SHA-256, for the player;
// nothing when it is a release of the code image the build was made from.
std::optional<std::string> CodeImageMismatch(std::string_view executable_sha256,
                                             std::string_view built_image,
                                             std::span<const DiscRelease> releases);

}  // namespace fm4_static_recomp
