#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
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
  std::vector<PromptTexture> textures;
  uint64_t hash = 1;
  for (const char* name : kPromptTextureNames)
    textures.push_back({name, hash++});
  return textures;
}

uint64_t HashOf(const std::vector<PromptTexture>& textures, std::string_view name) {
  for (const auto& texture : textures) {
    if (texture.name == name)
      return texture.hash;
  }
  return 0;
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
  REQUIRE(textures.size() == 15);
  // From a texture_dump_dir run on the main menu (HANDOFF, fifty-eighth session).
  CHECK(HashOf(textures, "A_Button_F3") == 0x95644C0B2202BE17ull);
  CHECK(HashOf(textures, "B_Button_F3") == 0x9771F6078D7987A1ull);
}

TEST_CASE("Keycaps are registered while the keyboard is in use", "[prompt_swap]") {
  REQUIRE_GAME_FILES();
  FakeRegistry registry;
  PromptSwap swap(Textures(), *Font(), DefaultControls(), registry.Hook());
  swap.Update(true, InputContext::kMenu);
  CHECK(registry.images.size() == 15);
  CHECK(registry.calls == 15);
  swap.Update(false, InputContext::kMenu);
  CHECK(registry.images.empty());
  CHECK(registry.calls == 30);
  swap.Update(false, InputContext::kMenu);
  CHECK(registry.calls == 30);
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
  PromptSwap swap(Textures(), *Font(), DefaultControls(), registry.Hook());
  swap.Update(true, InputContext::kMenu);
  const RuntimeImage& image = *registry.images.begin()->second;
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
