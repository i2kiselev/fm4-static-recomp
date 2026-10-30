#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "input/keycap.h"
#include "ui/hostui/ui_archive.h"
#include "ui/hostui/vector_font.h"
#include "test_game_root.h"

using fm4_static_recomp::hostui::UiArchive;
using fm4_static_recomp::hostui::VectorFont;
using namespace fm4_static_recomp::input;

namespace {

// The help bar's font (fontmap.xml: B_Swiss_721_Medium).
const VectorFont* Font() {
  static const std::optional<VectorFont> font = []() -> std::optional<VectorFont> {
    const auto archive = UiArchive::Open(TestGameRoot() / "Media/UI/Fonts.zip");
    if (!archive)
      return std::nullopt;
    const auto data = archive->Read("b_vector_aa.dt");
    return data ? VectorFont::Parse(*data) : std::nullopt;
  }();
  return font ? &*font : nullptr;
}

#define REQUIRE_FONT()                                             \
  if (!Font()) {                                                   \
    SKIP("Fonts.zip not found under " << TestGameRoot().string()); \
  }

struct Pixel {
  int r, g, b, a;
};

Pixel At(const Rgba& image, uint32_t x, uint32_t y) {
  const size_t i = (size_t(y) * image.size + x) * 4;
  return {image.pixels[i], image.pixels[i + 1], image.pixels[i + 2], image.pixels[i + 3]};
}

struct Box {
  int left = 1 << 30, top = 1 << 30, right = -1, bottom = -1;
  bool empty() const { return right < 0; }
};

// Pixels of the text: opaque and darker than #404040.
Box DarkBox(const Rgba& image) {
  Box box;
  for (uint32_t y = 0; y < image.size; ++y) {
    for (uint32_t x = 0; x < image.size; ++x) {
      const Pixel p = At(image, x, y);
      if (p.a > 200 && p.r < 0x40 && p.g < 0x40 && p.b < 0x40) {
        box.left = std::min(box.left, int(x));
        box.right = std::max(box.right, int(x));
        box.top = std::min(box.top, int(y));
        box.bottom = std::max(box.bottom, int(y));
      }
    }
  }
  return box;
}

int DarkCount(const Rgba& image, uint32_t y0, uint32_t y1) {
  int count = 0;
  for (uint32_t y = y0; y < y1; ++y) {
    for (uint32_t x = 0; x < image.size; ++x) {
      const Pixel p = At(image, x, y);
      count += p.a > 200 && p.r < 0x40;
    }
  }
  return count;
}

}  // namespace

TEST_CASE("Key labels are short and upper-case", "[keycap]") {
  CHECK(KeyLabel("Return") == "ENTER");
  CHECK(KeyLabel("Escape") == "ESC");
  CHECK(KeyLabel("Space") == "SPACE");
  CHECK(KeyLabel("Shift") == "SHIFT");
  CHECK(KeyLabel("Tab") == "TAB");
  CHECK(KeyLabel("Backspace") == "BKSP");
  CHECK(KeyLabel("Up") == kArrowUp);
  CHECK(KeyLabel("Down") == kArrowDown);
  CHECK(KeyLabel("Left") == kArrowLeft);
  CHECK(KeyLabel("Right") == kArrowRight);
  CHECK(KeyLabel("W") == "W");
  CHECK(KeyLabel("7") == "7");
  CHECK(KeyLabel("Numpad8") == "NUM 8");
  CHECK(KeyLabel("NumpadStar") == "NUM *");
  CHECK(KeyLabel("NumpadEnter") == "ENTER");
  CHECK(KeyLabel("PageUp") == "PGUP");
  CHECK(KeyLabel("PageDown") == "PGDN");
  CHECK(KeyLabel("PrintScreen") == "PRTSC");
  CHECK(KeyLabel("ScrollLock") == "SCRLK");
  CHECK(KeyLabel("CapsLock") == "CAPS");
  CHECK(KeyLabel("Delete") == "DEL");
  CHECK(KeyLabel("Semicolon") == ";");
  CHECK(KeyLabel("LBracket") == "[");
  CHECK(KeyLabel("Shift+W") == "SHIFT+W");
  CHECK(KeyLabel("Control+PageDown") == "CTRL+PGDN");
  // Arrows inside a combination are words: the font has no arrow glyphs.
  CHECK(KeyLabel("Shift+Up") == "SHIFT+UP");
}

// The SDK keyboard driver's key names (rex/ui/keybinds.cpp), apart from
// letters, digits and F-keys.
constexpr const char* kDriverKeyNames[] = {
    "Backtick",    "Minus",      "Plus",       "Comma",       "Period",      "Semicolon",
    "Slash",       "Backslash",  "LBracket",   "RBracket",    "Quote",       "Escape",
    "Return",      "Space",      "Tab",        "Backspace",   "Delete",      "Insert",
    "Home",        "End",        "PageUp",     "PageDown",    "Shift",       "Control",
    "Alt",         "Numpad0",    "Numpad9",    "NumpadEnter", "NumpadPlus",  "NumpadMinus",
    "NumpadStar",  "NumpadSlash", "PrintScreen", "Pause",     "CapsLock",    "NumLock",
    "ScrollLock",  "LMB",        "RMB",        "MMB",         "F12"};

TEST_CASE("Every key's label draws, words no smaller than ENTER", "[keycap]") {
  REQUIRE_FONT();
  for (const char* name : kDriverKeyNames) {
    const std::string label = KeyLabel(name);
    INFO(name << " -> " << label);
    CHECK(label.size() <= 5);
    const std::vector<std::string> labels = {label};
    const Box box = DarkBox(RenderKeycap(labels, 128, *Font()));
    REQUIRE_FALSE(box.empty());
    if (std::isalpha(uint8_t(label[0]))) {
      // ENTER's capitals are 20 of 128 pixels; the old SCROLLLOCK's were 9.
      CHECK(box.bottom - box.top >= 18);
    }
  }
}

TEST_CASE("A keycap is a light rounded key with a darker bottom edge", "[keycap]") {
  REQUIRE_FONT();
  const std::vector<std::string> labels = {"T"};
  const Rgba cap = RenderKeycap(labels, 128, *Font());
  REQUIRE(cap.size == 128);
  REQUIRE(cap.pixels.size() == 128u * 128 * 4);
  CHECK(At(cap, 0, 0).a == 0);
  CHECK(At(cap, 127, 127).a == 0);
  const Pixel body = At(cap, 13, 58);
  CHECK(body.a == 255);
  CHECK(std::abs(body.r - 0xD2) < 0x14);
  CHECK(body.r == body.g);
  const Pixel edge = At(cap, 32, 125);
  const Pixel above = At(cap, 32, 112);
  CHECK(edge.a == 255);
  CHECK(edge.r < above.r - 0x10);
}

TEST_CASE("The key text is dark and centred on the face", "[keycap]") {
  REQUIRE_FONT();
  const std::vector<std::string> labels = {"ESC"};
  const Rgba cap = RenderKeycap(labels, 128, *Font());
  const Box box = DarkBox(cap);
  REQUIRE_FALSE(box.empty());
  const int face_height = 128 - 128 * 3 / 32;
  CHECK(std::abs((box.left + box.right) / 2 - 64) <= 4);
  CHECK(std::abs((box.top + box.bottom) / 2 - face_height / 2) <= 4);
}

TEST_CASE("Long labels shrink to fit inside the margin", "[keycap]") {
  REQUIRE_FONT();
  for (const char* label : {"NUM 7", "SCROLLLOCK", "SHIFT+PAGEDOWN"}) {
    INFO(label);
    const std::vector<std::string> labels = {label};
    const Rgba cap = RenderKeycap(labels, 128, *Font());
    const Box box = DarkBox(cap);
    REQUIRE_FALSE(box.empty());
    CHECK(box.left >= 16);
    CHECK(box.right <= 128 - 16);
    CHECK(box.top >= 16);
  }
}

TEST_CASE("A narrower keycap keeps to its width, the text centred on it", "[keycap]") {
  REQUIRE_FONT();
  const std::vector<std::string> labels = {"TAB"};
  const Rgba cap = RenderKeycap(labels, 128, *Font(), 108);
  REQUIRE(cap.size == 128);
  for (uint32_t y = 0; y < 128; ++y) {
    CHECK(At(cap, 108, y).a == 0);
  }
  CHECK(At(cap, 4, 64).a == 255);
  const Box box = DarkBox(cap);
  REQUIRE_FALSE(box.empty());
  CHECK(std::abs((box.left + box.right) / 2 - 54) <= 4);
}

TEST_CASE("An arrow key shows a drawn arrow", "[keycap]") {
  REQUIRE_FONT();
  const std::vector<std::string> up = {KeyLabel("Up")};
  const Rgba cap = RenderKeycap(up, 128, *Font());
  const int face_middle = (128 - 128 * 3 / 32) / 2;
  const int upper = DarkCount(cap, 0, face_middle);
  const int lower = DarkCount(cap, face_middle, 128);
  CHECK(upper + lower > 200);
  CHECK(upper < lower);  // a triangle pointing up has its wide base below
}

TEST_CASE("Groups share one cap, and fall back to the first key when too small", "[keycap]") {
  REQUIRE_FONT();
  const std::vector<std::string> steer = {"A", "D"};
  const std::vector<std::string> first_only = {"A"};
  CHECK(RenderKeycap(steer, 128, *Font()).pixels != RenderKeycap(first_only, 128, *Font()).pixels);
  const std::vector<std::string> arrows = {KeyLabel("Up"), KeyLabel("Left"), KeyLabel("Down"),
                                           KeyLabel("Right")};
  const std::vector<std::string> up = {KeyLabel("Up")};
  CHECK(RenderKeycap(arrows, 32, *Font()).pixels != RenderKeycap(up, 32, *Font()).pixels);
  const std::vector<std::string> long_group = {"PAGEUP", "PAGEDOWN", "HOME", "END"};
  const std::vector<std::string> page_up = {"PAGEUP"};
  CHECK(RenderKeycap(long_group, 128, *Font()).pixels ==
        RenderKeycap(page_up, 128, *Font()).pixels);
}

// Hidden: writes sample keycaps as BMPs to FM4_KEYCAP_DIR for a look.
TEST_CASE("Sample keycaps", "[.keycap_images]") {
  REQUIRE_FONT();
  const char* dir = std::getenv("FM4_KEYCAP_DIR");
  REQUIRE(dir);
  const std::vector<std::vector<std::string>> samples = {
      {"ENTER"},
      {"ESC"},
      {"SPACE"},
      {"SHIFT"},
      {"TAB"},
      {"W"},
      {"A", "D"},
      {"W", "A", "S", "D"},
      {KeyLabel("Up"), KeyLabel("Left"), KeyLabel("Down"), KeyLabel("Right")},
      {KeyLabel("Up")},
      {KeyLabel("Left"), KeyLabel("Right")},
      {"NUM 8"},
      {"BKSP"}};
  for (size_t i = 0; i < samples.size(); ++i) {
    const Rgba cap = RenderKeycap(samples[i], 128, *Font());
    std::vector<uint8_t> file(54 + cap.pixels.size());
    auto put32 = [&](size_t at, uint32_t value) { std::memcpy(&file[at], &value, 4); };
    file[0] = 'B';
    file[1] = 'M';
    put32(2, uint32_t(file.size()));
    put32(10, 54);
    put32(14, 40);
    put32(18, cap.size);
    put32(22, uint32_t(-int32_t(cap.size)));
    file[26] = 1;
    file[28] = 32;
    for (size_t p = 0; p < cap.pixels.size(); p += 4) {
      file[54 + p] = cap.pixels[p + 2];
      file[54 + p + 1] = cap.pixels[p + 1];
      file[54 + p + 2] = cap.pixels[p];
      file[54 + p + 3] = cap.pixels[p + 3];
    }
    std::ofstream(std::filesystem::path(dir) / ("cap" + std::to_string(i) + ".bmp"),
                  std::ios::binary)
        .write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
  }
}
