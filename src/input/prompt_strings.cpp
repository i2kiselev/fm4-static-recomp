// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "input/prompt_strings.h"

#include <algorithm>
#include <array>
#include <vector>

#include "input/keycap.h"

namespace fm4_static_recomp::input {
namespace {

struct PromptStringKey {
  uint16_t key;
  bool driving;  // the string is about a race: the driving keys
};

// From the EN string tables: CarExperience (Autovista), InGame (race
// tutorials), Lobby, Main (Rivals, the tab strips' LB/RB), MainMenu (title
// screen), MyProfile (rewind help), Storefront, Telemetry, Tuning, UpgradeAreas.
constexpr std::array<PromptStringKey, 24> kKeys = {{
    {0x3D74, false},  // CarExperience IDS_Explore_Description
    {0x42FB, false},  // CarExperience IDS_Interact_Description1
    {0x437B, false},  // CarExperience IDS_Interact_Description2
    {0xBA58, false},  // CarExperience IDS_Getin_Description
    {0xDE8C, false},  // CarExperience IDS_Move_Description2
    {0xDF0C, false},  // CarExperience IDS_Move_Description1
    {0xEC34, false},  // CarExperience IDS_Look_Description2
    {0xEDB4, false},  // CarExperience IDS_Look_Description1
    {0x4D92, true},   // InGame IDS_Accelerate_Message
    {0x76B3, true},   // InGame IDS_View_Message
    {0xF762, true},   // InGame IDS_Steering_Message
    {0xFC27, false},  // Lobby IDS_OptionBucketNameDesc
    {0x5222, false},  // Main IDS_LB
    {0xD225, false},  // Main IDS_RB
    {0xC0F5, false},  // Main IDS_Rivals_Message
    {0x90AB, false},  // MainMenu IDS_A_Start
    {0x206F, true},   // MyProfile IDS_Rewind_Message
    {0x6877, true},   // MyProfile IDS_Controller_Discription
    {0x63B4, false},  // Storefront IDS_GamertagDefault
    {0xE49E, false},  // Storefront IDS_ManualKeywordDefault
    {0x2ADE, true},   // Telemetry IDS_Press_Dpad_Down
    {0xD422, false},  // Tuning IDS_SetupFileLocked_Remove_Msg
    {0xEC3B, false},  // UpgradeAreas IDS_Description_3
    {0xED3B, false},  // UpgradeAreas IDS_Description_1
}};

constexpr std::array<uint16_t, kKeys.size()> kKeyValues = [] {
  std::array<uint16_t, kKeys.size()> values{};
  for (size_t i = 0; i < kKeys.size(); ++i)
    values[i] = kKeys[i].key;
  return values;
}();

enum class Slot {
  kA,
  kB,
  kX,
  kY,
  kRB,
  kRT,
  kBack,
  kStart,
  kDpadDown,
  kLeftStick,
  kLeftStickUp,
  kLeftStickPress,
  kRightStick
};

struct Phrase {
  std::u16string_view from;
  std::u16string_view before;
  Slot slot;
  std::u16string_view after;
};

// Longest first, so a phrase inside a longer one does not take it apart.
constexpr Phrase kPhrases[] = {
    // The title screen's lines hug this text: a key name longer than A must not widen it.
    {u"Press A to start the game.", u"Press ", Slot::kA, u" to start"},
    {u"or pull the RIGHT TRIGGER", u"or ", Slot::kRT, u""},
    {u"Push forward on the LEFT STICK", u"Press ", Slot::kLeftStickUp, u""},
    {u"press the LEFT STICK (lean out)", u"press ", Slot::kLeftStickPress, u" (lean out)"},
    {u"HOLD DOWN THE RIGHT TRIGGER", u"HOLD DOWN ", Slot::kRT, u""},
    {u"PRESS THE BACK BUTTON", u"PRESS ", Slot::kBack, u""},
    {u"USE THE LEFT STICK", u"USE ", Slot::kLeftStick, u""},
    {u"PRESS D-PAD DOWN", u"PRESS ", Slot::kDpadDown, u""},
    {u"press the 'X' button", u"press ", Slot::kX, u""},
    {u"Press the RB button", u"Press ", Slot::kRB, u""},
    {u"press the B button", u"press ", Slot::kB, u""},
    {u"Press the A button", u"Press ", Slot::kA, u""},
    {u"press the A button", u"press ", Slot::kA, u""},
    {u"PRESS X BUTTON", u"PRESS ", Slot::kX, u""},
    {u"press START to", u"press ", Slot::kStart, u" to"},
    {u"Rewind button", u"Rewind key (", Slot::kY, u")"},
    {u"the LEFT STICK", u"", Slot::kLeftStick, u""},
    {u"the RIGHT STICK", u"", Slot::kRightStick, u""},
    {u"The RIGHT STICK", u"", Slot::kRightStick, u""},
    {u"Press A to", u"Press ", Slot::kA, u" to"},
};

// Strings that are a button name and nothing else, matched whole: as phrases
// they would take apart words in the other strings under these keys.
struct WholeText {
  std::u16string_view text;
  PadControl control;
};

constexpr WholeText kWholeTexts[] = {
    {u"LB", PadControl::kLeftShoulder},
    {u"RB", PadControl::kRightShoulder},
};

std::u16string Wide(std::string_view text) {
  return std::u16string(text.begin(), text.end());
}

std::string FirstKey(const BindingSet& set, PadControl control) {
  const std::string& bind = set.keys[size_t(control)];
  return KeyLabel(std::string_view(bind).substr(0, bind.find(',')));
}

// Keys in words: the game's fonts have no arrow glyphs.
std::string Words(const std::string& label) {
  if (label == kArrowUp)
    return "UP ARROW";
  if (label == kArrowDown)
    return "DOWN ARROW";
  if (label == kArrowLeft)
    return "LEFT ARROW";
  if (label == kArrowRight)
    return "RIGHT ARROW";
  return label;
}

// A stick's keys: "WASD", "A/D", or the arrow keys.
std::string Group(const BindingSet& set, std::initializer_list<PadControl> controls) {
  std::vector<std::string> labels;
  for (PadControl control : controls)
    labels.push_back(FirstKey(set, control));
  const bool arrows = std::all_of(labels.begin(), labels.end(), [](const std::string& label) {
    return label == kArrowUp || label == kArrowDown || label == kArrowLeft || label == kArrowRight;
  });
  if (arrows)
    return labels.size() == 4 ? "the arrow keys" : "LEFT/RIGHT ARROW";
  const bool letters = std::all_of(labels.begin(), labels.end(),
                                   [](const std::string& label) { return label.size() == 1; });
  std::string text;
  for (const std::string& label : labels) {
    if (!text.empty() && !(letters && labels.size() == 4))
      text += '/';
    text += Words(label);
  }
  return text;
}

std::string SlotText(Slot slot, const BindingSet& set, bool driving) {
  using enum PadControl;
  switch (slot) {
    case Slot::kA:
      return Words(FirstKey(set, kA));
    case Slot::kB:
      return Words(FirstKey(set, kB));
    case Slot::kX:
      return Words(FirstKey(set, kX));
    case Slot::kY:
      return Words(FirstKey(set, kY));
    case Slot::kRB:
      return Words(FirstKey(set, kRightShoulder));
    case Slot::kRT:
      return Words(FirstKey(set, kRightTrigger));
    case Slot::kBack:
      return Words(FirstKey(set, kBack));
    case Slot::kStart:
      return Words(FirstKey(set, kStart));
    case Slot::kDpadDown:
      return Words(FirstKey(set, kDpadDown));
    case Slot::kLeftStickUp:
      return Words(FirstKey(set, kLStickUp));
    case Slot::kLeftStickPress:
      return Words(FirstKey(set, kLStickPress));
    case Slot::kLeftStick:
      // While driving the left stick only steers.
      return driving ? Group(set, {kLStickLeft, kLStickRight})
                     : Group(set, {kLStickUp, kLStickLeft, kLStickDown, kLStickRight});
    case Slot::kRightStick:
      return Group(set, {kRStickUp, kRStickLeft, kRStickDown, kRStickRight});
  }
  return {};
}

}  // namespace

std::span<const uint16_t> PromptStringKeys() {
  return kKeyValues;
}

bool IsPromptStringKey(uint16_t key) {
  return std::find(kKeyValues.begin(), kKeyValues.end(), key) != kKeyValues.end();
}

std::optional<std::u16string> KeyboardText(uint16_t key, std::u16string_view text,
                                           const Controls& controls) {
  const auto found = std::find_if(kKeys.begin(), kKeys.end(),
                                  [key](const PromptStringKey& entry) { return entry.key == key; });
  if (found == kKeys.end())
    return std::nullopt;
  const BindingSet& set = found->driving ? controls.driving : controls.menu;
  for (const WholeText& whole : kWholeTexts) {
    if (text == whole.text)
      return Wide(Words(FirstKey(set, whole.control)));
  }
  std::u16string out(text);
  bool changed = false;
  for (const Phrase& phrase : kPhrases) {
    for (size_t at = out.find(phrase.from); at != std::u16string::npos;
         at = out.find(phrase.from, at)) {
      const std::u16string replacement = std::u16string(phrase.before) +
                                         Wide(SlotText(phrase.slot, set, found->driving)) +
                                         std::u16string(phrase.after);
      out.replace(at, phrase.from.size(), replacement);
      at += replacement.size();
      changed = true;
    }
  }
  if (!changed)
    return std::nullopt;
  return out;
}

}  // namespace fm4_static_recomp::input
