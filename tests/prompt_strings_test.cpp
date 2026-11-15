#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>

#include "input/controls.h"
#include "input/prompt_strings.h"

using namespace fm4_static_recomp::input;

namespace {

constexpr uint16_t kViewMessage = 0x76B3;    // InGame IDS_View_Message (driving)
constexpr uint16_t kInteract = 0x42FB;       // CarExperience IDS_Interact_Description1
constexpr uint16_t kMove = 0xDF0C;           // CarExperience IDS_Move_Description1
constexpr uint16_t kTelemetryExit = 0x2ADE;  // Telemetry IDS_Press_Dpad_Down (driving)
constexpr uint16_t kTitleStart = 0x90AB;     // MainMenu IDS_A_Start
constexpr uint16_t kExplore = 0x3D74;        // CarExperience IDS_Explore_Description
constexpr uint16_t kEngineTip = 0xED3B;      // UpgradeAreas IDS_Description_1
constexpr uint16_t kTabLeft = 0x5222;        // Main IDS_LB
constexpr uint16_t kTabRight = 0xD225;       // Main IDS_RB

std::u16string U16(std::string_view text) {
  return std::u16string(text.begin(), text.end());
}

std::optional<std::string> Ascii(const std::optional<std::u16string>& text) {
  if (!text)
    return std::nullopt;
  return std::string(text->begin(), text->end());
}

}  // namespace

TEST_CASE("A button phrase becomes the bound key", "[prompt_strings]") {
  const Controls controls = DefaultControls();
  CHECK(Ascii(KeyboardText(kViewMessage, U16("x PRESS THE BACK BUTTON y"), controls)) ==
        "x PRESS TAB y");
  CHECK(Ascii(KeyboardText(kInteract, U16("x press the A button or pull the RIGHT TRIGGER."),
                           controls)) == "x press ENTER or C.");
  CHECK(Ascii(KeyboardText(kMove, U16("Use the LEFT STICK to y"), controls)) == "Use NUM 8/NUM 4/NUM 2/NUM 6 to y");
  CHECK(Ascii(KeyboardText(kTelemetryExit, U16("PRESS D-PAD DOWN TO EXIT"), controls)) ==
        "PRESS G TO EXIT");
}

TEST_CASE("The title screen and the short button phrases", "[prompt_strings]") {
  const Controls controls = DefaultControls();
  CHECK(Ascii(KeyboardText(kTitleStart, U16("Press A to start the game."), controls)) ==
        "Press ENTER to start");
  CHECK(Ascii(KeyboardText(kExplore, U16("x, press START to bring up y"), controls)) ==
        "x, press P to bring up y");
  CHECK(Ascii(KeyboardText(kEngineTip, U16("Tip: Press the RB button to go"), controls)) ==
        "Tip: Press E to go");
}

TEST_CASE("The tab strip's LB and RB become the menu keys", "[prompt_strings]") {
  Controls controls = DefaultControls();
  CHECK(Ascii(KeyboardText(kTabLeft, U16("LB"), controls)) == "Q");
  CHECK(Ascii(KeyboardText(kTabRight, U16("RB"), controls)) == "E");
  controls.menu.keys[size_t(PadControl::kLeftShoulder)] = "PageUp,Q";
  CHECK(Ascii(KeyboardText(kTabLeft, U16("LB"), controls)) == "PGUP");
  // MyProfile's string under the same key.
  CHECK_FALSE(KeyboardText(kTabRight, U16("NUMBER OF PERFECT DRAFT SCORES"), controls));
}

TEST_CASE("A rebind changes the wording", "[prompt_strings]") {
  Controls controls = DefaultControls();
  controls.driving.keys[size_t(PadControl::kBack)] = "C,Tab";
  CHECK(Ascii(KeyboardText(kViewMessage, U16("PRESS THE BACK BUTTON TO"), controls)) ==
        "PRESS C TO");
}

TEST_CASE("Other strings stay as they are", "[prompt_strings]") {
  const Controls controls = DefaultControls();
  CHECK_FALSE(KeyboardText(0x1234, U16("PRESS THE BACK BUTTON"), controls));
  // A listed key that names no button in this table (keys repeat across tables).
  CHECK_FALSE(KeyboardText(kViewMessage, U16("SELECT/UNSELECT LAYER"), controls));
  CHECK(IsPromptStringKey(kViewMessage));
  CHECK_FALSE(IsPromptStringKey(0x1234));
}

TEST_CASE("Every listed string names a button the rules know", "[prompt_strings]") {
  const auto path = std::filesystem::path(FM4_TEST_STRINGS);
  if (!std::filesystem::exists(path)) {
    SKIP("EN.tsv not found at " FM4_TEST_STRINGS);
  }
  // table \t key \t name \t text
  std::map<uint16_t, bool> changed;
  for (uint16_t key : PromptStringKeys())
    changed[key] = false;
  std::ifstream file(path);
  std::string line;
  const Controls controls = DefaultControls();
  while (std::getline(file, line)) {
    std::istringstream fields(line);
    std::string table, key_text, name, text;
    std::getline(fields, table, '\t');
    std::getline(fields, key_text, '\t');
    std::getline(fields, name, '\t');
    std::getline(fields, text);
    const uint16_t key = uint16_t(std::stoul(key_text, nullptr, 16));
    if (changed.contains(key) && KeyboardText(key, U16(text), controls)) {
      changed[key] = true;
    }
  }
  for (const auto& [key, was_changed] : changed) {
    INFO("key " << std::hex << key);
    CHECK(was_changed);
  }
}
