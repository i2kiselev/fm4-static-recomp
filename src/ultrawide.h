// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace fm4_static_recomp {

// fm4_ultrawide (#21): the race camera keeps its 1280x720 targets and the presenter stretches the
// frame over the window. Wider than 16:9 (Hor+), the camera keeps its vertical field of view and
// its aspect grows by this factor; at most 2 (32:9, the widest supported: the wide artwork is
// drawn for it). Taller (Vert+), the camera keeps its horizontal view and sees more above and
// below; at least 0.9 (16:10, the narrowest supported). 1 for a 16:9 window or one not yet sized.
inline constexpr float kUltrawideMinScale = 0.9f;
inline constexpr float kUltrawideMaxScale = 2.0f;

inline float UltrawideScale(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0) {
    return 1.0f;
  }
  const float scale = (float(width) / float(height)) / (16.0f / 9.0f);
  // A 16:9 size within float noise of it (1366x768) changes nothing.
  if (std::abs(scale - 1.0f) < 0.005f) {
    return 1.0f;
  }
  return std::clamp(scale, kUltrawideMinScale, kUltrawideMaxScale);
}

// Vert+: the vertical field of view (radians) whose projection Y scale is the original's times
// scale, so that with the aspect times scale the X scale stays.
inline float VertPlusFov(float fov, float scale) {
  if (scale >= 1.0f) {
    return fov;
  }
  return 2.0f * std::atan(std::tan(0.5f * fov) / scale);
}

// What the SDK gets for a race view factor: the HUD squeeze on one axis (fh1_hud_squeeze,
// fh1_hud_squeeze_y) and the aspect the presenter fits the frame to the window by
// (present_source_aspect, 0 = the game's 16:9 with its bars).
struct UltrawideOutputs {
  float scale;
  float squeeze_x;
  float squeeze_y;
  float source_aspect;
  bool legacy_stretch;
};

inline UltrawideOutputs UltrawideOutputsFor(float scale, bool has_source_aspect = true,
                                           bool has_hud_squeeze_y = true) {
  if (scale > 1.0f) {
    return {scale, scale, 1.0f, has_source_aspect ? 16.0f / 9.0f * scale : 0.0f, !has_source_aspect};
  }
  if (scale < 1.0f && has_source_aspect && has_hud_squeeze_y) {
    return {scale, 1.0f, 1.0f / scale, 16.0f / 9.0f * scale, false};
  }
  return {1.0f, 1.0f, 1.0f, 0.0f, false};
}

// Decides, once per main loop tick, whether frames show the race view (stretched, HUD squeezed)
// or the game's own 16:9 screens (letterboxed). Wide as soon as the race camera is seen; back to
// 16:9 only after kNarrowAfterTicks ticks without it, so a camera cut or a dialog does not flicker.
class WideViewSwitch {
 public:
  static constexpr uint32_t kNarrowAfterTicks = 30;

  // True when wide() changed this tick.
  bool Tick(bool race_view_seen) {
    const bool was = wide_;
    if (race_view_seen) {
      ticks_without_ = 0;
      wide_ = true;
    } else if (wide_ && ++ticks_without_ > kNarrowAfterTicks) {
      wide_ = false;
    }
    return wide_ != was;
  }

  bool wide() const { return wide_; }

 private:
  bool wide_ = false;
  uint32_t ticks_without_ = 0;
};

// fm4_ultrawide's race UI: the game lays its UI out on a 1280x720 canvas (x -640..640, centre
// origin) that the presenter squeezes back to 16:9 in the middle of the wide frame, so the wide
// frame's edges are at +-640 * scale on the canvas. Placement maps canvas x to x' = scale * x +
// offset for a whole UI subtree.
struct XMap {
  float scale = 1.0f;
  float offset = 0.0f;

  float operator()(float x) const { return scale * x + offset; }
  float Inverse(float x) const { return (x - offset) / scale; }
  XMap Inverted() const { return {1.0f / scale, -offset / scale}; }
  // This map, then `next`.
  XMap Then(XMap next) const { return {next.scale * scale, next.scale * offset + next.offset}; }
  bool identity() const { return scale == 1.0f && offset == 0.0f; }
};

// A horizontal extent on the canvas; empty until something is added.
struct XSpan {
  float lo = std::numeric_limits<float>::infinity();
  float hi = -std::numeric_limits<float>::infinity();

  bool empty() const { return lo > hi; }
  void Add(float a, float b) {
    lo = std::min({lo, a, b});
    hi = std::max({hi, a, b});
  }
  void Add(const XSpan& other) {
    if (!other.empty()) Add(other.lo, other.hi);
  }
};

enum class UiPlacement {
  kUnknown,    // nothing drawn under it yet: its children decide
  kContainer,  // a group spanning the canvas: its children decide
  kStretch,    // a quad spanning the canvas: its ends at the canvas edges go to the window edges
  kLeft,       // moves with the window's left edge
  kCentre,     // stays in the middle
  kRight,      // moves with the window's right edge
};

inline constexpr float kUiCanvasHalfWidth = 640.0f;
// Content at least this wide spans the canvas (backdrops, bars, fades).
inline constexpr float kUiFullWidth = 1150.0f;
// UI this close belongs together (a border next to its panel).
inline constexpr float kUiTouch = 2.0f;
// A group reaching this far out reaches a canvas edge.
inline constexpr float kUiEdge = 600.0f;
// A quad end this far out belongs to the edge (a rule ending under the edge's widgets); a group
// reaching this far on both sides spans the canvas (the race HUD's widgets end at about +-575).
inline constexpr float kUiQuadEdge = 540.0f;
// UI within this distance of the middle, or centred on the middle within it, stays there (a
// countdown digit, the rewind prompt).
inline constexpr float kUiMiddle = 64.0f;
// UI reaching less than this past the middle belongs to its side (photo mode's button hints with
// the effects' extra buttons); a table across the middle reaches further.
inline constexpr float kUiCross = 160.0f;

// A text's extent along one axis of its element, around its origin, from the layout the game
// caches in the element: alignment (0 left/top, 1 centre, 2 right/bottom), measured size and the
// layout's offset (RE 8235E2C0, which bins text by the same box). Empty for an unmeasured text.
inline XSpan TextSpan(uint32_t align, float size, float offset, bool vertical) {
  XSpan span;
  if (!(size > 0.0f) || align > 2) return span;
  if (align == 1) {
    span.Add(-0.5f * size, 0.5f * size);
  } else if (vertical) {
    span.Add(offset, offset - size);
  } else {
    span.Add(offset, align == 0 ? size - offset : size + offset);
  }
  return span;
}

// A UI subtree's drawn content on the canvas (y up), before placement.
struct UiBox {
  XSpan x, y;

  bool empty() const { return x.empty(); }
  void Add(const UiBox& other) {
    x.Add(other.x);
    y.Add(other.y);
  }
  // Overlapping or within kUiTouch of each other.
  bool Touches(const UiBox& other) const {
    return !empty() && !other.empty() && x.lo <= other.x.hi + kUiTouch && other.x.lo <= x.hi + kUiTouch &&
           y.lo <= other.y.hi + kUiTouch && other.y.lo <= y.hi + kUiTouch;
  }
};

struct UiSibling {
  UiBox box;
  bool quad = false;  // a drawn quad (stretches when it spans the canvas)
};

// Where each child of one UI group goes. A child spanning the canvas, or reaching from its middle
// to one edge, stretches (a quad) or leaves it to its own children (a group); any other child
// moves as one. Children well across the canvas's middle or close to it stay, and so does every
// child touching one that stays (a table's frame, a panel's border); the rest go to the window edge
// on the side most of them lie on. Off-canvas children go further off.
inline std::vector<UiPlacement> PlaceUiSiblings(const std::vector<UiSibling>& siblings,
                                                bool keep_edge_bands = false) {
  std::vector<UiPlacement> placed(siblings.size(), UiPlacement::kUnknown);
  std::vector<size_t> open;
  for (size_t i = 0; i < siblings.size(); ++i) {
    const UiBox& box = siblings[i].box;
    if (box.empty()) continue;
    // A rotating telemetry gauge can overlap a cross-canvas footer without sharing its anchor.
    if (keep_edge_bands && box.y.lo <= -kUiQuadEdge && box.y.hi >= kUiQuadEdge) {
      constexpr float inner = kUiQuadEdge - kUiMiddle;
      if (box.x.lo <= -kUiQuadEdge && box.x.hi <= -inner) {
        placed[i] = UiPlacement::kLeft;
        continue;
      }
      if (box.x.hi >= kUiQuadEdge && box.x.lo >= inner) {
        placed[i] = UiPlacement::kRight;
        continue;
      }
    }
    const float lo = std::max(box.x.lo, -kUiCanvasHalfWidth), hi = std::min(box.x.hi, kUiCanvasHalfWidth);
    if (hi - lo >= kUiFullWidth) {
      placed[i] = siblings[i].quad ? UiPlacement::kStretch : UiPlacement::kContainer;
    } else if (box.x.lo > -kUiMiddle && box.x.hi < kUiMiddle) {
      placed[i] = UiPlacement::kCentre;
    } else if (lo < 0.0f && hi > 0.0f) {
      // From the middle to one edge (a button bar): it reaches that window edge.
      const float edge = siblings[i].quad ? kUiQuadEdge : kUiEdge;
      const bool left = box.x.lo <= -edge, right = box.x.hi >= edge;
      const bool spans = !siblings[i].quad && box.x.lo <= -kUiQuadEdge && box.x.hi >= kUiQuadEdge;
      if (spans || left != right) {
        placed[i] = siblings[i].quad ? UiPlacement::kStretch : UiPlacement::kContainer;
      } else if (std::min(-box.x.lo, box.x.hi) >= kUiCross ||
                 std::abs(box.x.lo + box.x.hi) < 2.0f * kUiMiddle) {
        placed[i] = UiPlacement::kCentre;
      } else {
        open.push_back(i);
      }
    } else {
      open.push_back(i);
    }
  }
  for (bool joined = true; joined;) {
    joined = false;
    for (size_t& i : open) {
      if (i == SIZE_MAX) continue;
      for (size_t j = 0; j < siblings.size(); ++j) {
        if (placed[j] == UiPlacement::kCentre && siblings[i].box.Touches(siblings[j].box)) {
          placed[i] = UiPlacement::kCentre;
          i = SIZE_MAX;
          joined = true;
          break;
        }
      }
    }
  }
  for (const size_t i : open) {
    if (i != SIZE_MAX) {
      placed[i] = siblings[i].box.x.lo + siblings[i].box.x.hi <= 0.0f ? UiPlacement::kLeft : UiPlacement::kRight;
    }
  }
  return placed;
}

inline XMap UiPlacementMap(UiPlacement placement, const XSpan& content, float scale) {
  const float edge_shift = (scale - 1.0f) * kUiCanvasHalfWidth;
  switch (placement) {
    case UiPlacement::kLeft:
      return {1.0f, -edge_shift};
    case UiPlacement::kRight:
      return {1.0f, edge_shift};
    case UiPlacement::kStretch: {
      // An end at a canvas edge moves with the window edge, an end inside the canvas stays.
      const auto end = [edge_shift](float x) {
        return x >= kUiQuadEdge ? x + edge_shift : x <= -kUiQuadEdge ? x - edge_shift : x;
      };
      const float a = (end(content.hi) - end(content.lo)) / (content.hi - content.lo);
      return {a, end(content.lo) - a * content.lo};
    }
    default:
      return {};
  }
}

inline constexpr float kUiCanvasHalfHeight = 360.0f;
inline constexpr float kUiPlacementYUnits = kUiCanvasHalfWidth / kUiCanvasHalfHeight;

inline std::vector<UiPlacement> PlaceUiSiblingsY(const std::vector<UiSibling>& siblings) {
  std::vector<UiSibling> rotated;
  rotated.reserve(siblings.size());
  for (const auto& [box, quad] : siblings) {
    rotated.push_back({{XSpan{box.y.lo * kUiPlacementYUnits, box.y.hi * kUiPlacementYUnits}, box.x}, quad});
  }
  auto placed = PlaceUiSiblings(rotated, /*keep_edge_bands=*/true);
  for (size_t i = 0; i < rotated.size(); ++i) {
    if (rotated[i].quad) continue;
    const XSpan& span = rotated[i].box.x;
    // The countdown can share a layer with just the top HUD before the bottom HUD appears.
    if ((placed[i] == UiPlacement::kRight && span.lo < kUiMiddle && span.hi >= kUiQuadEdge) ||
        (placed[i] == UiPlacement::kLeft && span.hi > -kUiMiddle && span.lo <= -kUiQuadEdge)) {
      placed[i] = UiPlacement::kContainer;
    }
  }
  return placed;
}

inline XMap UiPlacementMapY(UiPlacement placement, const XSpan& content, float race_scale) {
  const auto map = UiPlacementMap(placement, {content.lo * kUiPlacementYUnits, content.hi * kUiPlacementYUnits},
                                  1.0f / race_scale);
  return {map.scale, map.offset / kUiPlacementYUnits};
}

// A UI control's pixel viewport (the race minimap draws its map into one): the game projects it
// from the control's element in 16:9 pixels of the base viewport and clamps it there, so the
// element's placement is undone for the projection and applied here instead, on the canvas, then
// the centre is squeezed about the base viewport's centre as the HUD draws are. The width stays:
// the game builds the control's projection from it (the minimap's zoom and shape are only right
// with the 16:9 width), and the SDK's HUD squeeze narrows the draws about the viewport's centre.
// The span stays inside the base viewport: the game draws nothing into one starting left of it
// (the left-placed minimap from ~1.67 on: up to 16 guest pixels right of its place at 32:9).
// y is unchanged.
struct PixelSpan {
  int32_t x = 0;
  int32_t width = 0;
  bool operator==(const PixelSpan&) const = default;
};

inline PixelSpan PlaceUiViewportX(int32_t x, int32_t width, uint32_t base_x, uint32_t base_width,
                                  XMap map, float scale) {
  const float centre = float(base_x) + 0.5f * float(base_width);
  const float pixels_per_unit = float(base_width) / (2.0f * kUiCanvasHalfWidth);
  const float canvas = map((float(x) + 0.5f * float(width) - centre) / pixels_per_unit);
  const float placed = centre + canvas * pixels_per_unit / scale;
  const int32_t lowest = int32_t(base_x);
  const int32_t highest = std::max(int32_t(base_x + base_width) - width, lowest);
  return {std::clamp(int32_t(std::lround(placed - 0.5f * float(width))), lowest, highest), width};
}

inline PixelSpan PlaceUiViewportY(int32_t y, int32_t height, uint32_t base_y, uint32_t base_height,
                                  XMap map, float race_scale) {
  const float centre = float(base_y) + 0.5f * float(base_height);
  const float pixels_per_unit = float(base_height) / (2.0f * kUiCanvasHalfHeight);
  const float canvas = map((centre - (float(y) + 0.5f * float(height))) / pixels_per_unit);
  const float placed = centre - canvas * pixels_per_unit * race_scale;
  const int32_t lowest = int32_t(base_y);
  const int32_t highest = std::max(int32_t(base_y + base_height) - height, lowest);
  return {std::clamp(int32_t(std::lround(placed - 0.5f * float(height))), lowest, highest), height};
}

}  // namespace fm4_static_recomp
