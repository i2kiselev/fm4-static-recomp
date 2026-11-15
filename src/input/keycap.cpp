// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "input/keycap.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <utility>

namespace fm4_static_recomp::input {
namespace {

constexpr float kFaceTop = 0xE6, kFaceBottom = 0xBD, kEdge = 0x8D, kText = 0x1A;

std::string Upper(std::string_view text) {
  std::string out(text);
  for (char& c : out)
    c = char(std::toupper(uint8_t(c)));
  return out;
}

bool IsArrow(std::string_view label) {
  return label == kArrowUp || label == kArrowDown || label == kArrowLeft || label == kArrowRight;
}

// A coverage mask; (0, 0) is its top-left pixel.
struct Ink {
  int width = 0, height = 0;
  std::vector<float> alpha;
  float& at(int x, int y) { return alpha[size_t(y) * width + x]; }
};

Ink Blank(int width, int height) {
  return {width, height, std::vector<float>(size_t(width) * height, 0.0f)};
}

// The label's glyphs at pixels_per_em, trimmed to their ink.
Ink Text(std::string_view text, const hostui::VectorFont& font, float pixels_per_em) {
  struct Placed {
    hostui::GlyphBitmap bitmap;
    int x;
  };
  std::vector<Placed> placed;
  float pen = 0.0f;
  int left = 1 << 30, right = -(1 << 30), top = 1 << 30, bottom = -(1 << 30);
  for (char c : text) {
    const hostui::VectorFont::Glyph* glyph = font.Find(uint8_t(c));
    // FM4's space glyph carries a mesh far wider than the text: only its advance counts.
    if (c == ' ' || !glyph) {
      pen += pixels_per_em * (glyph ? glyph->advance : 0.25f);
      continue;
    }
    hostui::GlyphBitmap bitmap = font.Rasterize(*glyph, pixels_per_em);
    const int x = int(std::lround(pen)) + bitmap.left;
    if (bitmap.width && bitmap.height) {
      left = std::min(left, x);
      right = std::max(right, x + bitmap.width);
      top = std::min(top, bitmap.top);
      bottom = std::max(bottom, bitmap.top + bitmap.height);
      placed.push_back({std::move(bitmap), x});
    }
    pen += glyph->advance * pixels_per_em;
  }
  if (placed.empty())
    return {};
  Ink ink = Blank(right - left, bottom - top);
  for (const Placed& p : placed) {
    for (int y = 0; y < p.bitmap.height; ++y) {
      for (int x = 0; x < p.bitmap.width; ++x) {
        float& value = ink.at(p.x - left + x, p.bitmap.top - top + y);
        value = std::max(value, p.bitmap.alpha[size_t(y) * p.bitmap.width + x] / 255.0f);
      }
    }
  }
  return ink;
}

// A filled triangle in a side x side box, pointing along the arrow.
Ink Arrow(std::string_view arrow, int side) {
  Ink ink = Blank(side, side);
  constexpr int kSamples = 4;
  for (int y = 0; y < side; ++y) {
    for (int x = 0; x < side; ++x) {
      int inside = 0;
      for (int sy = 0; sy < kSamples; ++sy) {
        for (int sx = 0; sx < kSamples; ++sx) {
          // u along the arrow (0 base, 1 tip), v across it (-1..1).
          const float px = (x + (sx + 0.5f) / kSamples) / side;
          const float py = (y + (sy + 0.5f) / kSamples) / side;
          float u = 0.0f, v = 0.0f;
          if (arrow == kArrowUp)
            u = 1.0f - py, v = px * 2.0f - 1.0f;
          if (arrow == kArrowDown)
            u = py, v = px * 2.0f - 1.0f;
          if (arrow == kArrowLeft)
            u = 1.0f - px, v = py * 2.0f - 1.0f;
          if (arrow == kArrowRight)
            u = px, v = py * 2.0f - 1.0f;
          inside += std::abs(v) <= 1.0f - u;
        }
      }
      ink.at(x, y) = float(inside) / (kSamples * kSamples);
    }
  }
  return ink;
}

void Blit(Ink& target, const Ink& source, int x0, int y0) {
  for (int y = 0; y < source.height; ++y) {
    for (int x = 0; x < source.width; ++x) {
      const int tx = x0 + x, ty = y0 + y;
      if (tx >= 0 && ty >= 0 && tx < target.width && ty < target.height) {
        target.at(tx, ty) = std::max(target.at(tx, ty), source.alpha[size_t(y) * source.width + x]);
      }
    }
  }
}

// The arrows of a stick or the D-pad: one, a row, or up above left, down and right.
Ink Arrows(std::span<const std::string> labels, float width, float height) {
  const auto has = [&](std::string_view arrow) {
    return std::find(labels.begin(), labels.end(), arrow) != labels.end();
  };
  const bool upper_row = has(kArrowUp) && labels.size() > 1;
  const int columns = int(labels.size()) - (upper_row ? 1 : 0);
  const int rows = upper_row ? 2 : 1;
  const int side = int(
      std::min(width / (columns + 0.25f * (columns - 1)), height / (rows + 0.25f * (rows - 1))));
  const int gap = side / 4;
  Ink ink = Blank(columns * side + (columns - 1) * gap, rows * side + (rows - 1) * gap);
  int column = 0;
  for (std::string_view arrow : {kArrowLeft, kArrowUp, kArrowDown, kArrowRight}) {
    if (!has(arrow))
      continue;
    if (upper_row && arrow == kArrowUp) {
      Blit(ink, Arrow(arrow, side), (ink.width - side) / 2, 0);
      continue;
    }
    Blit(ink, Arrow(arrow, side), column++ * (side + gap), (rows - 1) * (side + gap));
  }
  return ink;
}

// Coverage of a rounded rectangle at pixel (x, y).
float RoundedRect(int x, int y, float width, float height, float radius) {
  constexpr int kSamples = 4;
  int inside = 0;
  for (int sy = 0; sy < kSamples; ++sy) {
    for (int sx = 0; sx < kSamples; ++sx) {
      const float px = x + (sx + 0.5f) / kSamples, py = y + (sy + 0.5f) / kSamples;
      if (px > width || py > height)
        continue;
      const float dx = std::max({radius - px, px - (width - radius), 0.0f});
      const float dy = std::max({radius - py, py - (height - radius), 0.0f});
      inside += dx * dx + dy * dy <= radius * radius;
    }
  }
  return float(inside) / (kSamples * kSamples);
}

std::string Joined(std::span<const std::string> labels) {
  const bool letters = std::all_of(labels.begin(), labels.end(),
                                   [](const std::string& label) { return label.size() == 1; });
  std::string text;
  for (const std::string& label : labels) {
    if (!text.empty() && !(letters && labels.size() > 2))
      text += ' ';
    text += label;
  }
  return text;
}

// The labels' ink fitted to width x height, letters no taller than letter_height.
Ink Fit(std::span<const std::string> labels, const hostui::VectorFont& font, float width,
        float height, float letter_height) {
  if (std::all_of(labels.begin(), labels.end(),
                  [](const std::string& label) { return IsArrow(label); })) {
    return labels.size() == 1 ? Arrow(labels[0], int(std::min(width, letter_height)))
                              : Arrows(labels, width, height);
  }
  const std::string text = Joined(labels);
  constexpr float kProbe = 100.0f;
  const Ink probe = Text(text, font, kProbe);
  if (!probe.width)
    return {};
  // A capital's height, so a lone "W" and "ESC" stand as tall.
  const Ink capital = Text("H", font, kProbe);
  float letter_scale = letter_height / std::max(capital.height, 1);
  // Punctuation at the letters' size is a speck ("." 7 of 128 pixels): it
  // grows, up to a capital's height.
  if (std::all_of(text.begin(), text.end(),
                  [](char c) { return c == ' ' || std::ispunct(uint8_t(c)); })) {
    letter_scale = std::min(letter_scale * 2.5f, letter_height / std::max(probe.height, 1));
  }
  const float scale = std::min({width / probe.width, height / probe.height, letter_scale}) * 0.97f;
  return Text(text, font, kProbe * scale);
}

}  // namespace

std::string KeyLabel(std::string_view bind_token) {
  // A combination: each key by its label, arrows in words (the fonts have no arrows).
  if (const size_t plus = bind_token.find('+'); plus != std::string_view::npos && plus > 0) {
    std::string out;
    for (size_t start = 0; start <= bind_token.size();) {
      const size_t end = std::min(bind_token.find('+', start), bind_token.size());
      const std::string_view part = bind_token.substr(start, end - start);
      if (!out.empty())
        out += '+';
      out += IsArrow(KeyLabel(part)) ? Upper(part) : KeyLabel(part);
      start = end + 1;
    }
    return out;
  }
  // Keyboard legends, so a key's name stays short on the cap and in prose.
  static constexpr std::array<std::pair<std::string_view, std::string_view>, 35> kNames = {{
      {"Return", "ENTER"},     {"NumpadEnter", "ENTER"},  {"Escape", "ESC"},
      {"Space", "SPACE"},      {"Shift", "SHIFT"},        {"Control", "CTRL"},
      {"Tab", "TAB"},          {"Backspace", "BKSP"},     {"Delete", "DEL"},
      {"Insert", "INS"},       {"PageUp", "PGUP"},        {"PageDown", "PGDN"},
      {"PrintScreen", "PRTSC"}, {"ScrollLock", "SCRLK"},  {"NumLock", "NUMLK"},
      {"CapsLock", "CAPS"},    {"Up", kArrowUp},          {"Down", kArrowDown},
      {"Left", kArrowLeft},    {"Right", kArrowRight},    {"Backtick", "`"},
      {"Minus", "-"},          {"Plus", "="},             {"Comma", ","},
      {"Period", "."},         {"Semicolon", ";"},        {"Slash", "/"},
      {"Backslash", "\\"},     {"LBracket", "["},         {"RBracket", "]"},
      {"Quote", "'"},          {"NumpadPlus", "NUM +"},   {"NumpadMinus", "NUM -"},
      {"NumpadStar", "NUM *"}, {"NumpadSlash", "NUM /"},
  }};
  for (const auto& [name, label] : kNames) {
    if (bind_token == name)
      return std::string(label);
  }
  if (bind_token.starts_with("Numpad") && bind_token.size() == 7) {
    return "NUM " + std::string(bind_token.substr(6));
  }
  return Upper(bind_token);
}

Rgba RenderKeycap(std::span<const std::string> labels, uint32_t size,
                  const hostui::VectorFont& font, uint32_t cap_width) {
  const float cap = float(cap_width ? std::min(cap_width, size) : size);
  const float edge = float(size * 3 / 32);
  const float face = float(size) - edge;
  const float radius = size * 0.2f;
  const float margin = float(size / 8);
  const float width = cap - 2 * margin, height = face - 2 * margin;
  Ink ink = Fit(labels, font, width, height, face * 0.42f);
  // WASD's capitals come to about a sixth of the cap; smaller does not read.
  if (labels.size() > 1 && ink.height < float(size) / 6) {
    ink = Fit(labels.first(1), font, width, height, face * 0.42f);
  }
  const int ink_x = int(std::lround((cap - ink.width) / 2.0f));
  const int ink_y = int(std::lround((face - ink.height) / 2.0f));

  Rgba out{size, std::vector<uint8_t>(size_t(size) * size * 4)};
  for (uint32_t y = 0; y < size; ++y) {
    const float t = std::clamp((y + 0.5f) / face, 0.0f, 1.0f);
    const float face_color = kFaceTop + (kFaceBottom - kFaceTop) * t;
    for (uint32_t x = 0; x < size; ++x) {
      const float edge_cover = RoundedRect(int(x), int(y), cap, float(size), radius);
      const float face_cover = RoundedRect(int(x), int(y), cap, face, radius);
      float alpha = face_cover + edge_cover * (1.0f - face_cover);
      float color = face_color * face_cover + kEdge * edge_cover * (1.0f - face_cover);
      const int ix = int(x) - ink_x, iy = int(y) - ink_y;
      if (ix >= 0 && iy >= 0 && ix < ink.width && iy < ink.height) {
        const float text = ink.at(ix, iy) * face_cover;
        alpha = text + alpha * (1.0f - text);
        color = kText * text + color * (1.0f - text);
      }
      const uint8_t value = alpha > 0.0f ? uint8_t(std::lround(color / alpha)) : 0;
      uint8_t* const pixel = &out.pixels[(size_t(y) * size + x) * 4];
      pixel[0] = pixel[1] = pixel[2] = value;
      pixel[3] = uint8_t(std::lround(alpha * 255.0f));
    }
  }
  return out;
}

}  // namespace fm4_static_recomp::input
