// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "ultrawide.h"

using fm4_static_recomp::PixelSpan;
using fm4_static_recomp::PlaceUiSiblings;
using fm4_static_recomp::PlaceUiViewportX;
using fm4_static_recomp::TextSpan;
using fm4_static_recomp::UiPlacement;
using fm4_static_recomp::UiPlacementMap;
using fm4_static_recomp::UiSibling;
using fm4_static_recomp::UltrawideScale;
using fm4_static_recomp::XMap;
using fm4_static_recomp::XSpan;
using fm4_static_recomp::WideViewSwitch;

TEST_CASE("The race view widens by the window's aspect over 16:9", "[ultrawide]") {
  CHECK(UltrawideScale(2560, 1080) == Catch::Approx(2560.0 / 1080.0 / (16.0 / 9.0)));
  CHECK(UltrawideScale(5120, 1440) == Catch::Approx(2.0));
  CHECK(UltrawideScale(1920, 1080) == 1.0f);
}

TEST_CASE("A window wider than 32:9 keeps the 32:9 view", "[ultrawide]") {
  CHECK(UltrawideScale(7680, 1440) == Catch::Approx(2.0));
}

TEST_CASE("A 16:9-or-narrower or unsized window keeps the console's view", "[ultrawide]") {
  CHECK(UltrawideScale(1280, 1024) == 1.0f);
  CHECK(UltrawideScale(0, 1080) == 1.0f);
  CHECK(UltrawideScale(2560, 0) == 1.0f);
}

TEST_CASE("The race view goes wide on its first tick", "[ultrawide]") {
  WideViewSwitch view;
  CHECK_FALSE(view.Tick(false));
  CHECK_FALSE(view.wide());
  CHECK(view.Tick(true));
  CHECK(view.wide());
  CHECK_FALSE(view.Tick(true));
}

TEST_CASE("Frames go back to 16:9 only after 30 ticks without the race view", "[ultrawide]") {
  WideViewSwitch view;
  view.Tick(true);
  for (uint32_t tick = 0; tick < WideViewSwitch::kNarrowAfterTicks; ++tick) {
    CHECK_FALSE(view.Tick(false));
  }
  CHECK(view.wide());
  CHECK(view.Tick(false));
  CHECK_FALSE(view.wide());
}

TEST_CASE("A short gap in the race view does not count towards going back", "[ultrawide]") {
  WideViewSwitch view;
  view.Tick(true);
  for (uint32_t tick = 0; tick < 20; ++tick) view.Tick(false);
  view.Tick(true);
  for (uint32_t tick = 0; tick < 20; ++tick) view.Tick(false);
  CHECK(view.wide());
}

UiSibling Quad(float x0, float x1, float y0, float y1) {
  UiSibling sibling{{}, true};
  sibling.box.x.Add(x0, x1);
  sibling.box.y.Add(y0, y1);
  return sibling;
}

UiSibling Group(float x0, float x1, float y0, float y1) {
  UiSibling sibling = Quad(x0, x1, y0, y1);
  sibling.quad = false;
  return sibling;
}

XSpan Span(float lo, float hi) {
  XSpan span;
  span.Add(lo, hi);
  return span;
}

TEST_CASE("UI on one side of the canvas goes to that window edge", "[ultrawide]") {
  // Race HUD: place on the left, laps on the right.
  const auto placed = PlaceUiSiblings({Quad(-600, -480, 250, 330), Quad(500, 620, 280, 350)});
  CHECK(placed == std::vector{UiPlacement::kLeft, UiPlacement::kRight});
  // 21:9: the canvas edges (+-640) land on +-840 in the squeezed UI.
  CHECK(UiPlacementMap(UiPlacement::kLeft, Span(-600, -480), 1.3125f)(-640) == Catch::Approx(-840));
  CHECK(UiPlacementMap(UiPlacement::kRight, Span(500, 620), 1.3125f)(640) == Catch::Approx(840));
}

TEST_CASE("UI across the middle stays, with what overlaps it", "[ultrawide]") {
  // A table across the middle, a frame line touching it, a bar below on the right.
  const auto placed = PlaceUiSiblings(
      {Group(-271, 587, 100, 240), Quad(-275, -273, 90, 250), Group(365, 640, -300, -280)});
  CHECK(placed == std::vector{UiPlacement::kCentre, UiPlacement::kCentre, UiPlacement::kRight});
}

TEST_CASE("A panel's border goes with its panel, however close to the middle", "[ultrawide]") {
  // Pause: the left panel's parts reach x -290; a red rule at the right edge.
  const auto placed = PlaceUiSiblings({Quad(-645, -535, 300, 316), Quad(-290, -289, -361, 361),
                                       Quad(-323, -305, -361, 361), Quad(575, 685, -295, -291)});
  CHECK(placed == std::vector{UiPlacement::kLeft, UiPlacement::kLeft, UiPlacement::kLeft,
                              UiPlacement::kRight});
}

TEST_CASE("A bar from the middle to one edge reaches that window edge", "[ultrawide]") {
  // Pause: the button bar's rule starts under the table and ends at the right edge.
  const auto placed = PlaceUiSiblings({Quad(-280, 640, -292, -291), Group(-280, 640, -300, -280)});
  CHECK(placed == std::vector{UiPlacement::kStretch, UiPlacement::kContainer});
  const XMap map = UiPlacementMap(UiPlacement::kStretch, Span(-280, 640), 1.3125f);
  CHECK(map(-280) == Catch::Approx(-280));
  CHECK(map(640) == Catch::Approx(840));
}

TEST_CASE("A rule ending near an edge keeps its gap to that edge", "[ultrawide]") {
  // Pause: the rule ends at 569, its red tail at 601..640 moves with the right edge.
  const auto placed = PlaceUiSiblings({Quad(-280, 569, -293, -292), Quad(601, 640, -293, -292)});
  CHECK(placed == std::vector{UiPlacement::kStretch, UiPlacement::kRight});
  const XMap map = UiPlacementMap(UiPlacement::kStretch, Span(-280, 569), 1.3125f);
  CHECK(map(-280) == Catch::Approx(-280));
  CHECK(map(569) == Catch::Approx(769));
}

TEST_CASE("A table across the middle ending near an edge stays whole", "[ultrawide]") {
  CHECK(PlaceUiSiblings({Group(-271, 587, 100, 240)}) == std::vector{UiPlacement::kCentre});
}

TEST_CASE("UI a few pixels apart does not belong together", "[ultrawide]") {
  // Pause: the menu list ends 7 px left of a table that stays.
  const auto placed = PlaceUiSiblings({Group(-535, -287, -300, 195), Group(-280, 587, -300, 240)});
  CHECK(placed == std::vector{UiPlacement::kLeft, UiPlacement::kCentre});
}

TEST_CASE("A full-width quad stretches to both window edges", "[ultrawide]") {
  CHECK(PlaceUiSiblings({Quad(-640, 640, -360, 360)}) == std::vector{UiPlacement::kStretch});
  const XMap map = UiPlacementMap(UiPlacement::kStretch, Span(-640, 640), 1.3125f);
  CHECK(map(-640) == Catch::Approx(-840));
  CHECK(map(640) == Catch::Approx(840));
  CHECK(map(0) == Catch::Approx(0).margin(1e-4));
}

TEST_CASE("A wide quad from one edge stretches to that edge only", "[ultrawide]") {
  CHECK(PlaceUiSiblings({Quad(-525, 640, 192, 308)}) == std::vector{UiPlacement::kStretch});
  const XMap map = UiPlacementMap(UiPlacement::kStretch, Span(-525, 640), 1.3125f);
  CHECK(map(-525) == Catch::Approx(-525));
  CHECK(map(640) == Catch::Approx(840));
}

TEST_CASE("A group moves as one; a full-width one leaves it to its children", "[ultrawide]") {
  // The race HUD's left widgets in one layer, and a layer across the canvas.
  const auto placed = PlaceUiSiblings({Group(-600, -380, -330, 330), Group(-640, 640, -360, 360)});
  CHECK(placed == std::vector{UiPlacement::kLeft, UiPlacement::kContainer});
}

TEST_CASE("A layer reaching both safe edges leaves it to its children", "[ultrawide]") {
  // The race HUD with the digital speedometer ends at 571, short of the full width.
  CHECK(PlaceUiSiblings({Group(-574, 571, -324, 318)}) == std::vector{UiPlacement::kContainer});
}

TEST_CASE("UI near the middle stays there", "[ultrawide]") {
  // The countdown digit's text origin at x 5, beside the race HUD's widgets.
  const auto placed =
      PlaceUiSiblings({Group(5, 5, 21, 21), Group(-574, -380, -324, -138), Group(338, 579, -326, -138)});
  CHECK(placed == std::vector{UiPlacement::kCentre, UiPlacement::kLeft, UiPlacement::kRight});
}

TEST_CASE("A text spans its measured width by its alignment", "[ultrawide]") {
  const auto span = [](XSpan s) { return std::vector{s.lo, s.hi}; };
  CHECK(span(TextSpan(0, 120, 0, false)) == std::vector{0.0f, 120.0f});
  CHECK(span(TextSpan(1, 120, 7, false)) == std::vector{-60.0f, 60.0f});
  CHECK(span(TextSpan(2, 120, -120, false)) == std::vector{-120.0f, 0.0f});
  CHECK(span(TextSpan(0, 20, 0, true)) == std::vector{-20.0f, 0.0f});
  CHECK(TextSpan(1, 0, 0, false).empty());
  CHECK(TextSpan(3, 120, 0, false).empty());
}

TEST_CASE("UI centred on the middle stays there", "[ultrawide]") {
  // The rewind prompt, wider than the middle, beside the race HUD's widgets.
  const auto placed = PlaceUiSiblings({Group(-150, 140, -323, -291), Group(-574, -380, -324, -138)});
  CHECK(placed == std::vector{UiPlacement::kCentre, UiPlacement::kLeft});
}

TEST_CASE("UI reaching a little past the middle stays on its side", "[ultrawide]") {
  // Photo mode's button hints, with the effects' extra buttons; a table across the middle.
  const auto placed = PlaceUiSiblings({Group(-106, 451, -329, -297), Group(-271, 587, 100, 240)});
  CHECK(placed == std::vector{UiPlacement::kRight, UiPlacement::kCentre});
}

TEST_CASE("Nothing drawn yet decides nothing", "[ultrawide]") {
  CHECK(PlaceUiSiblings({UiSibling{}}) == std::vector{UiPlacement::kUnknown});
}

TEST_CASE("Off-canvas UI stays off the wide window", "[ultrawide]") {
  const auto placed = PlaceUiSiblings({Quad(-898, -642, -37, 27), Quad(642, 900, 0, 10)});
  REQUIRE(placed == std::vector{UiPlacement::kLeft, UiPlacement::kRight});
  CHECK(UiPlacementMap(UiPlacement::kLeft, Span(-898, -642), 1.3125f)(-642) < -840);
}

TEST_CASE("A placement undoes and composes", "[ultrawide]") {
  const XMap stretch{1.3125f, 0.0f}, shift{1.0f, -200.0f};
  CHECK(stretch.Inverse(stretch(300)) == Catch::Approx(300));
  CHECK(stretch.Then(shift)(100) == Catch::Approx(1.3125 * 100 - 200));
  const XMap both = stretch.Then(shift);
  CHECK(both.Then(both.Inverted())(123) == Catch::Approx(123));
  CHECK(both.Inverted()(both(-77)) == Catch::Approx(-77));
  CHECK(XMap{}.identity());
  CHECK_FALSE(shift.identity());
}

TEST_CASE("A UI control's viewport follows its element into the squeezed frame", "[ultrawide]") {
  // The race minimap at 21:9: 65..258 of 1280 in 16:9, its element moved to the left edge.
  const float scale = 1.3125f;
  const XMap left = UiPlacementMap(UiPlacement::kLeft, Span(-575, -382), scale);
  // The width stays; the centre goes where the squeeze about it leaves the map 65 window pixels
  // from the left edge after the stretch, as in 16:9.
  const PixelSpan placed = PlaceUiViewportX(65, 193, 0, 1280, left, scale);
  CHECK(placed == PixelSpan{27, 193});
  CHECK((placed.x + 96.5 - 96.5 / scale) * scale == Catch::Approx(65).margin(1));
  CHECK(PlaceUiViewportX(65, 193, 0, 1280, XMap{}, 1.0f) == PixelSpan{65, 193});
  // An unplaced control's centre is squeezed about the screen's centre like the HUD.
  CHECK(PlaceUiViewportX(740, 200, 0, 1280, XMap{}, 2.0f) == PixelSpan{640, 200});
  // At 32:9 the left-placed minimap's viewport would start at -16: it stays on the screen.
  CHECK(PlaceUiViewportX(65, 193, 0, 1280,
                         UiPlacementMap(UiPlacement::kLeft, Span(-575, -382), 2.0f),
                         2.0f) == PixelSpan{0, 193});
  CHECK(PlaceUiViewportX(1022, 193, 0, 1280,
                         UiPlacementMap(UiPlacement::kRight, Span(382, 575), 2.0f),
                         2.0f) == PixelSpan{1087, 193});
}
