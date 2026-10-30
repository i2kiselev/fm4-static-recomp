#pragma once

#include <cstdlib>
#include <filesystem>

// The extracted Disc 1 the tests read: FM4_TEST_GAME_ROOT from the environment, so one test
// executable runs against each release, else the folder the build was configured with.
inline std::filesystem::path TestGameRoot() {
  const char* root = std::getenv("FM4_TEST_GAME_ROOT");
  return root && *root ? std::filesystem::path(root) : std::filesystem::path(FM4_TEST_GAME_ROOT);
}
