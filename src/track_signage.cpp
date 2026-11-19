// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "track_signage.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <map>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "input/bc3_encoder.h"

REXCVAR_DEFINE_BOOL(fm4_track_signage, true, "Forza Motorsport 4",
                    "TRACK SIGNAGE: show the PNGs of the signage folder (beside the config folder) on "
                    "the trackside boards they are named after; false leaves every board as the game's");

namespace fm4_static_recomp::signage {
namespace {

using rex::graphics::texture_replacement::RuntimeImage;
using rex::graphics::texture_replacement::SetRuntimeReplacement;

// The boards of Hockenheim, as the game's texture dump names their textures (all DXT5).
constexpr std::array kBoards = {
    Board{"hockenheim-santander-wall", 0x2D9F71FDE6D7A955, 1024, 128, 11,
          "The red wall along the start straight (alpha: where it shines)"},
    Board{"hockenheim-mobil1", 0xCE9A5CD46ACED489, 1024, 256, 11, "The Mobil 1 boards"},
    Board{"hockenheim-allianz-grey", 0xDED8C777F55CD9D4, 1024, 256, 11, "The grey Allianz boards"},
    Board{"hockenheim-allianz-blue", 0xFF24C02434B6A09E, 1024, 256, 11, "The blue Allianz boards"},
    Board{"hockenheim-hotel", 0x6A6B1279BA99AC74, 1024, 128, 11, "The HOTEL RESTAURANT TERRASSE sign"},
    Board{"hockenheim-ring-logo", 0xB83FA75D9F36DD4F, 256, 64, 9, "The HOCKENHEIMRING logo boards"},
    Board{"hockenheim-ring-name", 0xAE15675BAD55075C, 512, 64, 10, "The hockenheimring name boards"},
    Board{"hockenheim-bw-center", 0xEC8A94D87E2FD6D6, 1024, 128, 11, "The Baden-Wuerttemberg Center banner"},
    Board{"hockenheim-sponsors", 0x7A049FE19A2E7BBD, 1024, 512, 11,
          "The sponsor banners (four strips, top to bottom)"},
};

std::atomic<size_t> g_shown = 0;

char Lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }

// A level's pixels padded to a multiple of 4 (BC3 blocks), edges repeated.
std::vector<uint8_t> PadToBlocks(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, uint32_t& pw,
                                 uint32_t& ph) {
  pw = (w + 3) & ~3u;
  ph = (h + 3) & ~3u;
  if (pw == w && ph == h) return rgba;
  std::vector<uint8_t> out(size_t(pw) * ph * 4);
  for (uint32_t y = 0; y < ph; ++y) {
    for (uint32_t x = 0; x < pw; ++x) {
      const uint8_t* p = &rgba[(size_t(std::min(y, h - 1)) * w + std::min(x, w - 1)) * 4];
      std::copy(p, p + 4, &out[(size_t(y) * pw + x) * 4]);
    }
  }
  return out;
}

// The next level: a 2x2 average (alpha-weighted colour), down to 1 in each direction.
std::vector<uint8_t> Halve(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, uint32_t& nw, uint32_t& nh) {
  nw = std::max(w / 2, 1u);
  nh = std::max(h / 2, 1u);
  std::vector<uint8_t> out(size_t(nw) * nh * 4);
  for (uint32_t y = 0; y < nh; ++y) {
    for (uint32_t x = 0; x < nw; ++x) {
      float colour[3] = {}, alpha = 0;
      for (uint32_t dy = 0; dy < 2; ++dy) {
        for (uint32_t dx = 0; dx < 2; ++dx) {
          const uint32_t sx = std::min(x * 2 + dx, w - 1), sy = std::min(y * 2 + dy, h - 1);
          const uint8_t* p = &rgba[(size_t(sy) * w + sx) * 4];
          const float a = p[3] / 255.0f;
          for (int c = 0; c < 3; ++c) colour[c] += p[c] * a;
          alpha += a;
        }
      }
      uint8_t* o = &out[(size_t(y) * nw + x) * 4];
      for (int c = 0; c < 3; ++c) o[c] = alpha > 0 ? uint8_t(std::lround(colour[c] / alpha)) : 0;
      o[3] = uint8_t(std::lround(alpha / 4 * 255));
    }
  }
  return out;
}

}  // namespace

std::span<const Board> Boards() { return kBoards; }

const Board* FindBoard(std::string_view stem) {
  for (const Board& board : kBoards) {
    if (board.file.size() == stem.size() &&
        std::equal(stem.begin(), stem.end(), board.file.begin(), [](char a, char b) { return Lower(a) == b; })) {
      return &board;
    }
  }
  return nullptr;
}

uint32_t ScaleFor(const Board& board, uint32_t width, uint32_t height) {
  uint32_t scale = 1;
  while (scale < 4 && width >= board.width * scale * 2 && height >= board.height * scale * 2) scale *= 2;
  return scale;
}

std::vector<uint8_t> Resample(std::span<const uint8_t> rgba, uint32_t width, uint32_t height, uint32_t dw,
                              uint32_t dh) {
  std::vector<uint8_t> out(size_t(dw) * dh * 4);
  for (uint32_t y = 0; y < dh; ++y) {
    const float fy = std::clamp((y + 0.5f) * height / dh - 0.5f, 0.0f, float(height - 1));
    const uint32_t y0 = uint32_t(fy), y1 = std::min(y0 + 1, height - 1);
    const float ty = fy - y0;
    for (uint32_t x = 0; x < dw; ++x) {
      const float fx = std::clamp((x + 0.5f) * width / dw - 0.5f, 0.0f, float(width - 1));
      const uint32_t x0 = uint32_t(fx), x1 = std::min(x0 + 1, width - 1);
      const float tx = fx - x0;
      for (int c = 0; c < 4; ++c) {
        auto at = [&](uint32_t px, uint32_t py) { return float(rgba[(size_t(py) * width + px) * 4 + c]); };
        const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
        const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
        out[(size_t(y) * dw + x) * 4 + c] = uint8_t(std::lround(top + (bottom - top) * ty));
      }
    }
  }
  return out;
}

std::shared_ptr<const RuntimeImage> BuildImage(const Board& board, std::span<const uint8_t> rgba, uint32_t width,
                                               uint32_t height) {
  const uint32_t scale = ScaleFor(board, width, height);
  auto image = std::make_shared<RuntimeImage>();
  image->format = rex::graphics::xenos::TextureFormat::k_DXT4_5;
  image->width = board.width * scale;
  image->height = board.height * scale;
  uint32_t w = image->width, h = image->height;
  std::vector<uint8_t> level = Resample(rgba, width, height, w, h);
  // Every level down to 1x1 (at least the board's own levels).
  for (;;) {
    uint32_t pw = 0, ph = 0;
    const std::vector<uint8_t> padded = PadToBlocks(level, w, h, pw, ph);
    const std::vector<uint8_t> blocks = input::EncodeBc3(padded, pw, ph);
    image->data.insert(image->data.end(), blocks.begin(), blocks.end());
    ++image->levels;
    if (w == 1 && h == 1) break;
    uint32_t nw = 0, nh = 0;
    level = Halve(level, w, h, nw, nh);
    w = nw;
    h = nh;
  }
  return image;
}

std::string Readme() {
  std::string text =
      "TRACK SIGNAGE\r\n"
      "\r\n"
      "Put a PNG here named after a board below, and races show it on that board (it changes\r\n"
      "within a few seconds, even mid-race). Delete it to get the game's board back.\r\n"
      "The image is stretched to the board's shape: best at the size given, or 2x or 4x it\r\n"
      "for a sharper board. Transparent parts let the board's background show through as dark.\r\n"
      "--fm4_track_signage=false (a launch option) turns them all off.\r\n"
      "\r\n";
  for (const Board& board : kBoards) {
    text += std::string(board.file) + ".png  (" + std::to_string(board.width) + " x " +
            std::to_string(board.height) + ")  " + std::string(board.what) + "\r\n";
  }
  return text;
}

size_t ShownCount() { return g_shown; }

void SetShownCount(size_t count) { g_shown = count; }  // from the watcher

}  // namespace fm4_static_recomp::signage
