// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "mixer_options.h"

#include <fstream>
#include <iterator>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem/content_override.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(fm4_keep_music_on_crash, false, "Forza Motorsport 4",
                    "Race music keeps playing through hard crashes instead of stopping for a few "
                    "seconds (restart required)");
REXCVAR_DEFINE_STRING(fm4_music_filter, "default", "Forza Motorsport 4",
                      "High-pass on the game's own music in the tracks' music zones: default (200 Hz, as on a "
                      "console) or light (50 Hz, keeps more bass); restart required");

namespace fm4_static_recomp {
namespace {

constexpr std::string_view kGuestPath = "game:\\media\\audio\\audiomixersnapshots.xml";

size_t FindOnce(std::string_view text, std::string_view what, size_t from = 0, size_t to = std::string_view::npos) {
  const size_t at = text.find(what, from);
  if (at == std::string_view::npos || at + what.size() > to) return std::string_view::npos;
  const size_t again = text.find(what, at + 1);
  return (again != std::string_view::npos && again + what.size() <= to) ? std::string_view::npos : at;
}

bool KeepMusicOnCrash(std::string& text) {
  const size_t crash = FindOnce(text, "<Crash ");
  const size_t crash_end = FindOnce(text, "</Crash>");
  if (crash == std::string::npos || crash_end == std::string::npos || crash_end < crash) return false;
  const size_t music = FindOnce(text, "<GameMusic ", crash, crash_end);
  if (music == std::string::npos) return false;
  const size_t tag_end = text.find('>', music);
  constexpr std::string_view kActive = "active=\"1\"";
  const size_t active = FindOnce(text, kActive, music, tag_end);
  if (active == std::string::npos) return false;
  text[active + kActive.size() - 2] = '0';
  return true;
}

bool LightFilter(std::string& text) {
  constexpr std::string_view kDefault = "zonefilterlevel=\"200.0\"";
  constexpr std::string_view kLight = "zonefilterlevel=\"50.00\"";
  const size_t at = FindOnce(text, kDefault);
  if (at == std::string::npos) return false;
  text.replace(at, kDefault.size(), kLight);
  return true;
}

}  // namespace

MixerPatchResult PatchMixerSnapshots(std::string_view xml, bool keep_music_on_crash,
                                     bool light_filter) {
  MixerPatchResult result{std::string(xml)};
  if (keep_music_on_crash) result.crash_applied = KeepMusicOnCrash(result.text);
  if (light_filter) result.filter_applied = LightFilter(result.text);
  return result;
}

void InstallMixerOptions(const std::filesystem::path& game_data_root) {
  const bool keep_music = REXCVAR_GET(fm4_keep_music_on_crash);
  const std::string filter = REXCVAR_GET(fm4_music_filter);
  if (filter != "default" && filter != "light") {
    REXLOG_WARN("fm4_music_filter: unknown value '{}', using default", filter);
  }
  const bool light = filter == "light";
  if (!keep_music && !light) return;
  const auto path = game_data_root / "Media" / "audio" / "AudioMixerSnapshots.xml";
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    REXLOG_WARN("Music options: cannot read {}", path.string());
    return;
  }
  const std::string xml((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const auto result = PatchMixerSnapshots(xml, keep_music, light);
  if (keep_music && !result.crash_applied) {
    REXLOG_WARN("MUSIC ON CRASH not applied: the game's mixer file is not the disc's");
  }
  if (light && !result.filter_applied) {
    REXLOG_WARN("MUSIC FILTER not applied: the game's mixer file is not the disc's");
  }
  if (result.crash_applied || result.filter_applied) {
    rex::filesystem::SetContentOverride(kGuestPath,
                                        std::vector<uint8_t>(result.text.begin(), result.text.end()));
  }
}

}  // namespace fm4_static_recomp
