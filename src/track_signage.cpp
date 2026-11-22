// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "track_signage.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <rex/cvar.h>

#include "input/bc3_encoder.h"

REXCVAR_DEFINE_BOOL(fm4_track_signage, true, "Forza Motorsport 4",
                    "TRACK SIGNAGE (PC OPTIONS > SIGNAGE): show your pictures on the track boards "
                    "chosen there; false shows every board's default");

namespace fm4_static_recomp::signage {
namespace {

using rex::graphics::texture_replacement::RuntimeImage;

// Hockenheim's boards: its road and grass (each one texture, tiled over the whole circuit), then every sign
// texture of Media\Tracks\Hockenheim\bin.zip (all DXT5).
constexpr std::array kHockenheim = {
    Board{"road", "ROAD SURFACE", 0x00000159, 0x5E1088ECE2F85B61, 512, 512, 10},
    Board{"grass", "GRASS", 0x000001FB, 0xBB1899D1710E3C0E, 512, 512, 10},
    Board{"santander-wall", "SANTANDER WALL, START STRAIGHT", 0x000000D8, 0x2D9F71FDE6D7A955, 1024, 128, 11},
    Board{"santander-small", "SANTANDER SIGN, PIT BUILDING", 0x000002E7, 0x1678DF29672849B0, 512, 64, 10},
    Board{"santander-large", "SANTANDER LOGO ON THE RUN-OFF", 0x000001C8, 0xB70F9761CF8DD84D, 1024, 256, 11},
    Board{"santander-checker", "SANTANDER BANNER, START GANTRY", 0x00000126, 0x5A9CF1C94A94FD0C, 256, 256, 9},
    Board{"allianz-blue", "ALLIANZ BARRIER BOARDS", 0x000001B9, 0xFF24C02434B6A09E, 1024, 256, 11},
    Board{"allianz-blue-2", "ALLIANZ LOGO ON THE TRACK", 0x00000564, 0x15A6B33A147320CB, 1024, 256, 11},
    Board{"allianz-grey", "ALLIANZ BOARD, GREY", 0x000001BA, 0xDED8C777F55CD9D4, 1024, 256, 11},
    Board{"mobil1", "MOBIL 1 BOARD", 0x00000117, 0xCE9A5CD46ACED489, 1024, 256, 11},
    Board{"mobil1-2", "MOBIL 1 BILLBOARD, TRACKSIDE", 0x000001F3, 0x098F94CFD87D3F4B, 1024, 256, 11},
    Board{"mobil1-panel", "MOBIL 1 LOGO ON THE RUN-OFF", 0x0000031F, 0xFE656FE857FAAD1C, 1024, 512, 11},
    Board{"kaercher-eni", "KAERCHER / ENI, GRANDSTAND", 0x000001EC, 0xD2906713754C6237, 1024, 256, 11},
    Board{"ring-logo-banner", "HOCKENHEIMRING LOGO BOARDS", 0x0000046D, 0x892A4C3677BEE204, 1024, 128, 11},
    Board{"ring-name", "HOCKENHEIMRING NAME WALL", 0x0000012A, 0xAE15675BAD55075C, 512, 64, 10},
    Board{"ring-logo", "HOCKENHEIMRING LOGO, PIT ROOF", 0x00000143, 0xB83FA75D9F36DD4F, 256, 64, 9},
    Board{"ring-more-than-racing", "MORE THAN RACING, STAND ROOF", 0x00000145, 0xDD95D3B1A4D7927E, 1024, 256, 11},
    Board{"ring-small", "HOCKENHEIMRING TOWER SIGN", 0x00000147, 0x8563DC9F43AB1347, 256, 128, 9},
    Board{"sachs-kurve", "SACHS KURVE BARRIER", 0x000001EA, 0x65AF13287CA4A0EA, 1024, 128, 11},
    Board{"dunlop", "DUNLOP FLAGS", 0x00000369, 0xDF351A53FFA1CFF1, 128, 256, 9},
    Board{"kiosk", "KIOSK SIGN, INFIELD", 0x00000111, 0x45509000F94D3A15, 128, 64, 8},
    Board{"panasonic", "PANASONIC LOGO ON THE RUN-OFF", 0x000001BD, 0x2F8AB1611810DEEE, 1024, 256, 11},
    Board{"agip", "AGIP BOARD", 0x00000321, 0x8E5C9AE43DD395D3, 512, 256, 10},
    Board{"mercedes-benz", "MERCEDES-BENZ, STAND ROOF", 0x00000296, 0x592DFB0FCD2A0797, 1024, 128, 11},
    Board{"bridgestone", "BRIDGESTONE, ON THE RUN-OFF", 0x00000378, 0x9526FB95FCD60BDA, 1024, 256, 11},
    Board{"rico-anthes", "RICO ANTHES, ON THE ROAD", 0x00000728, 0x505A8FEA98C9F771, 512, 128, 10},
    Board{"betonschutzwand", "BETONSCHUTZWAND BARRIER", 0x000006AA, 0xC8932575514FBCD4, 1024, 128, 11},
    Board{"hotel", "HOTEL TERRASSE, GRANDSTAND", 0x00000120, 0x6A6B1279BA99AC74, 1024, 128, 11},
    Board{"bw-center", "BADEN-WUERTTEMBERG TOWER", 0x00000106, 0xEC8A94D87E2FD6D6, 1024, 128, 11},
    Board{"sponsors", "SPONSOR BANNERS, STAND ROOF", 0x00000114, 0x7A049FE19A2E7BBD, 1024, 512, 11},
    Board{"adac", "ADAC BANNERS ON THE FENCE", 0x000003D9, 0x6902B00DCFF49633, 256, 128, 9},
    Board{"info-board", "INFO BOARD ON A POLE", 0x000004E5, 0x4B0C3856E21613E5, 128, 64, 8},
    Board{"ausfahrt", "AUSFAHRT SIGN, PIT EXIT", 0x000001F5, 0x24E1F22852654B24, 256, 64, 9},
};

constexpr std::array kTracks = {
    Track{"hockenheim", "HOCKENHEIMRING", "Media/Tracks/Hockenheim/bin.zip", kHockenheim},
};

// A level padded to a multiple of 4 (BC3 blocks), edges repeated.
Image PadToBlocks(const Image& image) {
  const uint32_t pw = (image.width + 3) & ~3u, ph = (image.height + 3) & ~3u;
  if (pw == image.width && ph == image.height) return image;
  Image out{pw, ph, std::vector<uint8_t>(size_t(pw) * ph * 4)};
  for (uint32_t y = 0; y < ph; ++y) {
    for (uint32_t x = 0; x < pw; ++x) {
      const uint8_t* p =
          &image.rgba[(size_t(std::min(y, image.height - 1)) * image.width + std::min(x, image.width - 1)) * 4];
      std::copy(p, p + 4, &out.rgba[(size_t(y) * pw + x) * 4]);
    }
  }
  return out;
}

// The next level: a 2x2 average (alpha-weighted colour), down to 1 in each direction.
Image Halve(const Image& image) {
  const uint32_t w = image.width, h = image.height;
  Image out{std::max(w / 2, 1u), std::max(h / 2, 1u), {}};
  out.rgba.resize(size_t(out.width) * out.height * 4);
  for (uint32_t y = 0; y < out.height; ++y) {
    for (uint32_t x = 0; x < out.width; ++x) {
      float colour[3] = {}, alpha = 0;
      for (uint32_t dy = 0; dy < 2; ++dy) {
        for (uint32_t dx = 0; dx < 2; ++dx) {
          const uint32_t sx = std::min(x * 2 + dx, w - 1), sy = std::min(y * 2 + dy, h - 1);
          const uint8_t* p = &image.rgba[(size_t(sy) * w + sx) * 4];
          const float a = p[3] / 255.0f;
          for (int c = 0; c < 3; ++c) colour[c] += p[c] * a;
          alpha += a;
        }
      }
      uint8_t* o = &out.rgba[(size_t(y) * out.width + x) * 4];
      for (int c = 0; c < 3; ++c) o[c] = alpha > 0 ? uint8_t(std::lround(colour[c] / alpha)) : 0;
      o[3] = uint8_t(std::lround(alpha / 4 * 255));
    }
  }
  return out;
}

// Byte offset of block (x, y) in an Xbox 360 tiled surface `blocks_wide` blocks across, for
// blocks of 2^log_bpp bytes (the console's 2D tiling).
uint32_t TiledOffset(uint32_t x, uint32_t y, uint32_t blocks_wide, uint32_t log_bpp) {
  const uint32_t aligned = (blocks_wide + 31) & ~31u;
  const uint32_t macro = ((x >> 5) + (y >> 5) * (aligned >> 5)) << (log_bpp + 7);
  const uint32_t micro = ((x & 7) + ((y & 6) << 2)) << log_bpp;
  const uint32_t offset =
      macro + ((micro & ~15u) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4);
  return ((offset & ~511u) << 3) + ((offset & 448) << 2) + (offset & 63) + ((y & 16) << 7) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
}

void Rgb565(uint16_t c, int out[3]) {
  out[0] = ((c >> 11) & 31) * 255 / 31;
  out[1] = ((c >> 5) & 63) * 255 / 63;
  out[2] = (c & 31) * 255 / 31;
}

}  // namespace

std::span<const Track> Tracks() { return kTracks; }

const Track* FindTrack(std::string_view key) {
  for (const Track& track : kTracks) {
    if (track.key == key) return &track;
  }
  return nullptr;
}

const Board* FindBoard(const Track& track, std::string_view key) {
  for (const Board& board : track.boards) {
    if (board.key == key) return &board;
  }
  return nullptr;
}

std::string ChoiceKey(const Track& track, const Board& board) {
  return std::string(track.key) + "." + std::string(board.key);
}

std::string FormatChoices(const Choices& choices) {
  std::string text = "# TRACK SIGNAGE: the picture each board shows (PC OPTIONS > SIGNAGE writes this file)\n";
  for (const auto& [key, file] : choices) {
    std::string quoted;
    for (char c : file) {
      if (c == '"' || c == '\\') quoted += '\\';
      quoted += c;
    }
    text += key + " = \"" + quoted + "\"\n";
  }
  return text;
}

Choices ParseChoices(std::string_view text) {
  if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
  const auto trim = [](std::string_view v) {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
    return v;
  };
  Choices choices;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const size_t eq = line.find('=');
    if (line.empty() || line.front() == '#' || eq == std::string_view::npos) continue;
    const std::string_view key = trim(line.substr(0, eq));
    std::string_view value = trim(line.substr(eq + 1));
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') continue;
    value = value.substr(1, value.size() - 2);
    std::string file;
    for (size_t i = 0; i < value.size(); ++i) {
      if (value[i] == '\\' && i + 1 < value.size()) ++i;
      file += value[i];
    }
    // A plain file name of the signage folder, for a board that exists.
    const size_t dot = key.find('.');
    if (dot == std::string_view::npos || file.empty() || file.find_first_of("/\\:") != std::string::npos) {
      continue;
    }
    const Track* track = FindTrack(key.substr(0, dot));
    if (!track || !FindBoard(*track, key.substr(dot + 1))) continue;
    choices[std::string(key)] = file;
  }
  return choices;
}

uint32_t ScaleFor(const Board& board, uint32_t width, uint32_t height) {
  uint32_t scale = 1;
  while (scale < 4 && width >= board.width * scale * 2 && height >= board.height * scale * 2) scale *= 2;
  return scale;
}

Image Resample(const Image& image, uint32_t dw, uint32_t dh) {
  const uint32_t width = image.width, height = image.height;
  Image out{dw, dh, std::vector<uint8_t>(size_t(dw) * dh * 4)};
  for (uint32_t y = 0; y < dh; ++y) {
    const float fy = std::clamp((y + 0.5f) * height / dh - 0.5f, 0.0f, float(height - 1));
    const uint32_t y0 = uint32_t(fy), y1 = std::min(y0 + 1, height - 1);
    const float ty = fy - y0;
    for (uint32_t x = 0; x < dw; ++x) {
      const float fx = std::clamp((x + 0.5f) * width / dw - 0.5f, 0.0f, float(width - 1));
      const uint32_t x0 = uint32_t(fx), x1 = std::min(x0 + 1, width - 1);
      const float tx = fx - x0;
      for (int c = 0; c < 4; ++c) {
        auto at = [&](uint32_t px, uint32_t py) { return float(image.rgba[(size_t(py) * width + px) * 4 + c]); };
        const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
        const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
        out.rgba[(size_t(y) * dw + x) * 4 + c] = uint8_t(std::lround(top + (bottom - top) * ty));
      }
    }
  }
  return out;
}

Image Fit(const Image& image, uint32_t max_width, uint32_t max_height) {
  if (!image.width || !image.height) return image;
  const float scale = std::min({1.0f, float(max_width) / image.width, float(max_height) / image.height});
  const uint32_t dw = std::max(1u, uint32_t(std::lround(image.width * scale)));
  const uint32_t dh = std::max(1u, uint32_t(std::lround(image.height * scale)));
  // Halve first (cheap, and smoother than one big bilinear step), then resample to the size.
  Image source = image;
  while (source.width / 2 >= dw && source.height / 2 >= dh && source.width > 1 && source.height > 1) {
    source = Halve(source);
  }
  return Resample(source, dw, dh);
}

std::shared_ptr<const RuntimeImage> BuildImage(const Board& board, const Image& picture) {
  const uint32_t scale = ScaleFor(board, picture.width, picture.height);
  auto image = std::make_shared<RuntimeImage>();
  image->format = rex::graphics::xenos::TextureFormat::k_DXT4_5;
  image->width = board.width * scale;
  image->height = board.height * scale;
  Image level = Resample(picture, image->width, image->height);
  // Every level down to 1x1 (at least the board's own levels).
  for (;;) {
    const Image padded = PadToBlocks(level);
    const std::vector<uint8_t> blocks = input::EncodeBc3(padded.rgba, padded.width, padded.height);
    image->data.insert(image->data.end(), blocks.begin(), blocks.end());
    ++image->levels;
    if (level.width == 1 && level.height == 1) break;
    level = Halve(level);
  }
  return image;
}

Image DecodeTiledDxt5(std::span<const uint8_t> data, uint32_t width, uint32_t height) {
  const uint32_t bw = std::max(1u, width / 4), bh = std::max(1u, height / 4);
  Image out{bw * 4, bh * 4, std::vector<uint8_t>(size_t(bw) * 4 * bh * 4 * 4)};
  for (uint32_t by = 0; by < bh; ++by) {
    for (uint32_t bx = 0; bx < bw; ++bx) {
      const uint32_t at = TiledOffset(bx, by, bw, 4);
      if (size_t(at) + 16 > data.size()) continue;
      // The console's blocks are 16-bit words, byte-swapped against the PC layout.
      uint8_t b[16];
      for (int k = 0; k < 16; k += 2) {
        b[k] = data[at + k + 1];
        b[k + 1] = data[at + k];
      }
      const int a0 = b[0], a1 = b[1];
      uint64_t abits = 0;
      for (int k = 0; k < 6; ++k) abits |= uint64_t(b[2 + k]) << (8 * k);
      int alpha[8] = {a0, a1};
      for (int k = 2; k < 8; ++k) {
        alpha[k] = a0 > a1 ? ((8 - k) * a0 + (k - 1) * a1) / 7
                           : (k < 6 ? ((6 - k) * a0 + (k - 1) * a1) / 5 : (k == 6 ? 0 : 255));
      }
      const uint16_t c0 = uint16_t(b[8] | b[9] << 8), c1 = uint16_t(b[10] | b[11] << 8);
      int pal[4][3];
      Rgb565(c0, pal[0]);
      Rgb565(c1, pal[1]);
      for (int c = 0; c < 3; ++c) {
        pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
        pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
      }
      const uint32_t cbits =
          uint32_t(b[12]) | uint32_t(b[13]) << 8 | uint32_t(b[14]) << 16 | uint32_t(b[15]) << 24;
      for (int p = 0; p < 16; ++p) {
        const int px = int(bx) * 4 + p % 4, py = int(by) * 4 + p / 4;
        uint8_t* o = &out.rgba[(size_t(py) * out.width + px) * 4];
        const int ci = (cbits >> (2 * p)) & 3;
        for (int c = 0; c < 3; ++c) o[c] = uint8_t(pal[ci][c]);
        o[3] = uint8_t(alpha[(abits >> (3 * p)) & 7]);
      }
    }
  }
  return out;
}

}  // namespace fm4_static_recomp::signage
