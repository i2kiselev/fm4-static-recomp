// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "mixer_options.h"
#include "test_game_root.h"

using fm4_static_recomp::PatchMixerSnapshots;

namespace {

const std::string kSnapshots =
    "<MixSnapshots>\r\n"
    "  <Crash threshold=\"1.0\" drop=\"2.0\">\r\n"
    "    <Collision      active=\"0\" destvolume=\"0.000000\" fadetime=\"0.000000\"/>\r\n"
    "    <GameMusic      active=\"1\" destvolume=\"0.000000\" fadetime=\"0.000000\" returntime=\"2.000000\"/>\r\n"
    "    <MenuMusic      active=\"0\" destvolume=\"0.000000\" fadetime=\"0.000000\"/>\r\n"
    "  </Crash>\r\n"
    "  <Topspeed threshold=\"0.700000\" holdtime=\"200\">\r\n"
    "    <GameMusic      active=\"1\" destvolume=\"1.400000\" fadetime=\"0.000000\"/>\r\n"
    "  </Topspeed>\r\n"
    "  <MusicHighpass fadeinrate=\"3.0\" fadeoutrate=\"33.0\" zonefilterlevel=\"200.0\"/>\r\n"
    "</MixSnapshots>\r\n";

}  // namespace

TEST_CASE("Crash option silences only the crash snapshot's music duck", "[mixer]") {
  const auto result = PatchMixerSnapshots(kSnapshots, true, false);
  CHECK(result.crash_applied);
  CHECK_FALSE(result.filter_applied);
  CHECK(result.text.size() == kSnapshots.size());
  CHECK(result.text.find("<GameMusic      active=\"0\" destvolume=\"0.000000\"") != std::string::npos);
  CHECK(result.text.find("<GameMusic      active=\"1\" destvolume=\"1.400000\"") != std::string::npos);
  CHECK(result.text.find("<Collision      active=\"0\"") != std::string::npos);
  CHECK(result.text.find("zonefilterlevel=\"200.0\"") != std::string::npos);
}

TEST_CASE("Filter option sets 50 Hz", "[mixer]") {
  const auto result = PatchMixerSnapshots(kSnapshots, false, true);
  CHECK(result.filter_applied);
  CHECK_FALSE(result.crash_applied);
  CHECK(result.text.size() == kSnapshots.size());
  CHECK(result.text.find("zonefilterlevel=\"50.00\"") != std::string::npos);
  CHECK(result.text.find("<GameMusic      active=\"1\" destvolume=\"0.000000\"") != std::string::npos);
}

TEST_CASE("Both options off return the file unchanged", "[mixer]") {
  const auto result = PatchMixerSnapshots(kSnapshots, false, false);
  CHECK(result.text == kSnapshots);
  CHECK_FALSE(result.crash_applied);
  CHECK_FALSE(result.filter_applied);
}

TEST_CASE("A modded mixer file is left alone", "[mixer]") {
  std::string modded = kSnapshots;
  modded.replace(modded.find("200.0"), 5, "50.0");
  auto result = PatchMixerSnapshots(modded, true, true);
  CHECK_FALSE(result.filter_applied);
  CHECK(result.crash_applied);
  CHECK(result.text.find("zonefilterlevel=\"50.0\"") != std::string::npos);

  const std::string no_crash = "<MixSnapshots>\r\n  <GameMusic active=\"1\"/>\r\n</MixSnapshots>\r\n";
  result = PatchMixerSnapshots(no_crash, true, false);
  CHECK_FALSE(result.crash_applied);
  CHECK(result.text == no_crash);
}

TEST_CASE("The disc's mixer file takes both options at its own size", "[mixer][disc]") {
  const auto path = TestGameRoot() / "Media/audio/AudioMixerSnapshots.xml";
  if (!std::filesystem::is_regular_file(path)) {
    SKIP("Media/audio/AudioMixerSnapshots.xml not found under " << TestGameRoot().string());
  }
  std::ifstream file(path, std::ios::binary);
  const std::string xml((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const auto result = PatchMixerSnapshots(xml, true, true);
  CHECK(result.crash_applied);
  CHECK(result.filter_applied);
  CHECK(result.text.size() == xml.size());
}
