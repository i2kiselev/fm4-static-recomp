#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <rex/graphics/pipeline/texture/fm4_runtime_replacement.h>

#include "input/keycap.h"
#include "input/prompt_swap.h"
#include "ui/hostui/ui_archive.h"
#include "ui/hostui/vector_font.h"
#include "test_game_root.h"

using fm4_static_recomp::hostui::UiArchive;
using fm4_static_recomp::hostui::VectorFont;
using rex::graphics::texture_replacement::RuntimeImage;
using namespace fm4_static_recomp::input;

namespace {

std::optional<UiArchive> Archive(const char* name) {
  return UiArchive::Open(TestGameRoot() / "Media/UI" / name);
}

const VectorFont* Font() {
  static const std::optional<VectorFont> font = []() -> std::optional<VectorFont> {
    const auto archive = Archive("Fonts.zip");
    const auto data = archive ? archive->Read("b_vector_aa.dt") : std::nullopt;
    return data ? VectorFont::Parse(*data) : std::nullopt;
  }();
  return font ? &*font : nullptr;
}

#define REQUIRE_GAME_FILES()                                                       \
  if (!Font() || !Archive("Textures.zip")) {                                       \
    SKIP("Fonts.zip or Textures.zip not found under " << TestGameRoot().string()); \
  }

struct FakeRegistry {
  std::map<uint64_t, std::shared_ptr<const RuntimeImage>> images;
  int calls = 0;
  PromptSwap::Registry Hook() {
    return [this](uint64_t hash, std::shared_ptr<const RuntimeImage> image) {
      ++calls;
      if (image) {
        images[hash] = std::move(image);
      } else {
        images.erase(hash);
      }
    };
  }
};

std::vector<PromptTexture> Textures() {
  return LoadPromptTextures(*Archive("Textures.zip"));
}

const PromptTexture& TextureOf(const std::vector<PromptTexture>& textures,
                               std::string_view name) {
  for (const auto& texture : textures) {
    if (texture.name == name)
      return texture;
  }
  FAIL("no texture " << name);
  throw;
}

uint64_t HashOf(const std::vector<PromptTexture>& textures, std::string_view name) {
  return TextureOf(textures, name).hash;
}

// The alpha of a texel of a BC3 image's top level.
int Bc3Alpha(const RuntimeImage& image, uint32_t x, uint32_t y) {
  const uint8_t* block = &image.data[((size_t(y / 4) * (image.width / 4)) + x / 4) * 16];
  const int a0 = block[0], a1 = block[1];
  uint64_t bits = 0;
  for (int i = 0; i < 6; ++i)
    bits |= uint64_t(block[2 + i]) << (8 * i);
  const int k = int(bits >> (3 * ((y % 4) * 4 + x % 4))) & 7;
  if (k < 2)
    return k ? a1 : a0;
  return a0 > a1 ? ((8 - k) * a0 + (k - 1) * a1) / 7
         : k < 6 ? ((6 - k) * a0 + (k - 1) * a1) / 5
                 : (k == 6 ? 0 : 255);
}

// The top level of a registered image against a keycap of these labels.
bool Shows(const RuntimeImage& image, std::vector<std::string> labels) {
  const Rgba cap = RenderKeycap(labels, image.width, *Font());
  return std::equal(cap.pixels.begin(), cap.pixels.end(), image.data.begin());
}

}  // namespace

TEST_CASE("Each prompt icon's hash is the guest base level's", "[prompt_swap]") {
  REQUIRE_GAME_FILES();
  const auto textures = LoadPromptTextures(*Archive("Textures.zip"));
  REQUIRE(textures.size() == 18);
  // From a texture_dump_dir run on the main menu (HANDOFF, fifty-eighth session).
  CHECK(HashOf(textures, "A_Button_F3") == 0x95644C0B2202BE17ull);
  CHECK(HashOf(textures, "B_Button_F3") == 0x9771F6078D7987A1ull);
  const PromptTexture& tab = TextureOf(textures, "HUD/TEL_TabIcon_L");
  CHECK(tab.format == rex::graphics::xenos::TextureFormat::k_DXT4_5);
  CHECK(tab.width == 44);
  CHECK(tab.height == 24);
  CHECK(tab.levels == 1);
  CHECK(TextureOf(textures, "A_Button_F3").format ==
        rex::graphics::xenos::TextureFormat::k_8_8_8_8);
}

TEST_CASE("Keycaps are registered while the keyboard is in use", "[prompt_swap]") {
  REQUIRE_GAME_FILES();
  FakeRegistry registry;
  const auto textures = Textures();
  PromptSwap swap(textures, *Font(), DefaultControls(), registry.Hook());
  swap.Update(true, InputContext::kMenu);
  // The two BACK icons are one texture, as are the two RB ones.
  std::set<uint64_t> hashes;
  for (const auto& texture : textures)
    hashes.insert(texture.hash);
  CHECK(hashes.size() == 16);
  CHECK(registry.images.size() == hashes.size());
  CHECK(registry.calls == 18);
  swap.Update(false, InputContext::kMenu);
  CHECK(registry.images.empty());
  CHECK(registry.calls == 36);
  swap.Update(false, InputContext::kMenu);
  CHECK(registry.calls == 36);
}

TEST_CASE("The keycaps follow the input context", "[prompt_swap]") {
  REQUIRE_GAME_FILES();
  FakeRegistry registry;
  const auto textures = Textures();
  PromptSwap swap(textures, *Font(), DefaultControls(), registry.Hook());
  swap.Update(true, InputContext::kMenu);
  const uint64_t a = HashOf(textures, "A_Button_F3");
  CHECK(Shows(*registry.images.at(a), {"ENTER"}));
  swap.Update(true, InputContext::kDriving);
  CHECK(Shows(*registry.images.at(a), {"SPACE"}));
  // Steering shows only the keys that steer.
  CHECK(Shows(*registry.images.at(HashOf(textures, "L_Thumbstick_F3")), {"A", "D"}));
}

TEST_CASE("A rebind shows the new key", "[prompt_swap]") {
  REQUIRE_GAME_FILES();
  FakeRegistry registry;
  const auto textures = Textures();
  Controls controls = DefaultControls();
  controls.menu.keys[size_t(PadControl::kA)] = "Space";
  PromptSwap swap(textures, *Font(), controls, registry.Hook());
  swap.Update(true, InputContext::kMenu);
  CHECK(Shows(*registry.images.at(HashOf(textures, "A_Button_F3")), {"SPACE"}));
}

TEST_CASE("Switching back reuses the keycaps", "[prompt_swap]") {
  REQUIRE_GAME_FILES();
  FakeRegistry registry;
  const auto textures = Textures();
  PromptSwap swap(textures, *Font(), DefaultControls(), registry.Hook());
  const uint64_t a = HashOf(textures, "A_Button_F3");
  swap.Update(true, InputContext::kMenu);
  const auto first = registry.images.at(a);
  swap.Update(true, InputContext::kDriving);
  swap.Update(true, InputContext::kMenu);
  CHECK(registry.images.at(a) == first);
}

TEST_CASE("A keycap carries its smaller levels", "[prompt_swap]") {
  REQUIRE_GAME_FILES();
  FakeRegistry registry;
  const auto textures = Textures();
  PromptSwap swap(textures, *Font(), DefaultControls(), registry.Hook());
  swap.Update(true, InputContext::kMenu);
  const RuntimeImage& image = *registry.images.at(HashOf(textures, "A_Button_F3"));
  CHECK(image.format == rex::graphics::xenos::TextureFormat::k_8_8_8_8);
  CHECK(image.width == 128);
  CHECK(image.levels == 8);
  CHECK(image.data.size() ==
        size_t(4) * (128 * 128 + 64 * 64 + 32 * 32 + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2 + 1));
}

// Hidden: prints the icons' hashes, to compare with texture_dump_dir names.
TEST_CASE("Prompt icon hashes", "[.prompt_hashes]") {
  REQUIRE_GAME_FILES();
  for (const PromptTexture& texture : LoadPromptTextures(*Archive("Textures.zip"))) {
    std::printf("%016llX %s\n", static_cast<unsigned long long>(texture.hash),
                texture.name.c_str());
  }
}

TEST_CASE("The BACK keycaps end where the pad's BACK circle ends", "[prompt_swap]") {
  // The game draws its home icon right after the BACK icon, whose circle ends
  // at x 26 of 32; a full-width keycap would run into it.
  REQUIRE_GAME_FILES();
  FakeRegistry registry;
  const auto textures = Textures();
  PromptSwap swap(textures, *Font(), DefaultControls(), registry.Hook());
  swap.Update(true, InputContext::kMenu);
  const auto alpha_at = [&](std::string_view name, uint32_t x, uint32_t y) {
    const RuntimeImage& image = *registry.images.at(HashOf(textures, name));
    return image.data[(size_t(y) * image.width + x) * 4 + 3];
  };
  for (const char* back : {"Back_Button_F3", "Back_Button_D_F3"}) {
    INFO(back);
    for (uint32_t y = 0; y < 128; ++y) {
      CHECK(alpha_at(back, 108, y) == 0);
    }
    CHECK(alpha_at(back, 4, 64) == 255);
  }
  CHECK(alpha_at("A_Button_F3", 120, 64) == 255);
}

TEST_CASE("The telemetry tabs show the driving D-pad keys in BC3", "[prompt_swap]") {
  REQUIRE_GAME_FILES();
  FakeRegistry registry;
  const auto textures = Textures();
  PromptSwap swap(textures, *Font(), DefaultControls(), registry.Hook());
  swap.Update(true, InputContext::kDriving);
  const RuntimeImage& left = *registry.images.at(HashOf(textures, "HUD/TEL_TabIcon_L"));
  const RuntimeImage& right = *registry.images.at(HashOf(textures, "HUD/TEL_TabIcon_R"));
  for (const RuntimeImage* image : {&left, &right}) {
    CHECK(image->format == rex::graphics::xenos::TextureFormat::k_DXT4_5);
    CHECK(image->width == 176);
    CHECK(image->height == 96);
    // 176x96, 88x48 and the icon's own 44x24.
    CHECK(image->levels == 3);
    CHECK(image->data.size() == size_t(16) * (44 * 24 + 22 * 12 + 11 * 6));
  }
  // Chevron at the outer edge, keycap where the D-pad was, clear between.
  CHECK(Bc3Alpha(left, 12, 50) == 255);
  CHECK(Bc3Alpha(left, 124, 48) == 255);
  CHECK(Bc3Alpha(left, 62, 48) == 0);
  CHECK(Bc3Alpha(right, 175 - 12, 50) == 255);
  CHECK(Bc3Alpha(right, 48, 48) == 255);
  CHECK(Bc3Alpha(right, 175 - 62, 48) == 0);
  // The same in menus: the telemetry's keys are the driving ones.
  swap.Update(true, InputContext::kMenu);
  CHECK(registry.images.at(HashOf(textures, "HUD/TEL_TabIcon_L"))->data == left.data);
}

TEST_CASE("A rebind of the driving D-pad redraws the telemetry tab", "[prompt_swap]") {
  REQUIRE_GAME_FILES();
  const auto textures = Textures();
  const uint64_t tab = HashOf(textures, "HUD/TEL_TabIcon_L");
  FakeRegistry defaults, rebound;
  PromptSwap first(textures, *Font(), DefaultControls(), defaults.Hook());
  Controls controls = DefaultControls();
  controls.driving.keys[size_t(PadControl::kDpadLeft)] = "Q";
  PromptSwap second(textures, *Font(), controls, rebound.Hook());
  first.Update(true, InputContext::kDriving);
  second.Update(true, InputContext::kDriving);
  CHECK(defaults.images.at(tab)->data != rebound.images.at(tab)->data);
  CHECK(defaults.images.at(HashOf(textures, "HUD/TEL_TabIcon_R"))->data ==
        rebound.images.at(HashOf(textures, "HUD/TEL_TabIcon_R"))->data);
}

TEST_CASE("The telemetry exit hint's D-pad goes on keyboard", "[prompt_swap]") {
  REQUIRE_GAME_FILES();
  FakeRegistry registry;
  const auto textures = Textures();
  PromptSwap swap(textures, *Font(), DefaultControls(), registry.Hook());
  swap.Update(true, InputContext::kDriving);
  const RuntimeImage& image = *registry.images.at(HashOf(textures, "HUD/Icon_D_Pad_UpDown"));
  CHECK(image.format == rex::graphics::xenos::TextureFormat::k_DXT4_5);
  CHECK(image.width == 24);
  CHECK(image.levels == 1);
  REQUIRE(image.data.size() == size_t(16) * 6 * 6);
  for (uint32_t y = 0; y < 24; ++y) {
    for (uint32_t x = 0; x < 24; ++x)
      CHECK(Bc3Alpha(image, x, y) == 0);
  }
}
