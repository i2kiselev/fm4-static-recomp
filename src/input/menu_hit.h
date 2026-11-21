// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace fm4_static_recomp::input {

// The game's UI space: 1280 x 720, origin at the centre, x right, y up.
struct UiPoint {
  float x = 0, y = 0;
};

// Where the game's picture is in the window (letterbox bars outside it), in window pixels.
struct ContentRect {
  float left = 0, top = 0, width = 0, height = 0;
};

// The picture's rectangle in a window of this size: the whole window, or a centred 16:9 box when the
// presenter letterboxes.
ContentRect PictureRect(float window_width, float window_height, bool letterbox);

// A window pixel to UI space; nullopt outside the picture.
std::optional<UiPoint> ToUi(float x, float y, const ContentRect& picture);

// One item of a menu list, as the game lays it out (UI space).
struct MenuItem {
  bool shown = false;
  float min_x = 0, min_y = 0, max_x = 0, max_y = 0;  // the item's drawn bounds
  // A slider row (TUNE SETUP): its bar's ends (UI x) and where its value sits on it (0..1).
  bool slider = false;
  float bar_left = 0, bar_right = 0, fraction = 0;
  uint32_t id = 0;  // the game's object, for whoever acts on it (0 in tests)
  bool spinner = false;  // a value that steps through choices (a click steps to the next)
  // Which of the list's entries the item shows: a scrolling carousel recycles its items as it moves, and
  // the focus counts entries, not items. -1: the item's own place in the list (lists that do not recycle).
  int index = -1;
};

// The entry an item shows (its index, or its place when it has none).
inline int EntryOf(const struct MenuList& list, int item);

// A click on a spinner row: -1 on its left arrow (the arrow pair sits at the row's right end), +1 on its
// right arrow or anywhere else on the row (the next value).
int SpinnerClick(const MenuItem& item, float x);

// Whether a point on a slider row is on its bar (with a little slack at the ends).
bool OnSliderBar(const MenuItem& item, UiPoint point);
// Where on the bar (0..1) a pointer x asks the value to be.
float SliderTarget(const MenuItem& item, float x);
// The press that brings the value toward the target: +1 right, -1 left, 0 when within half a step.
int SliderStep(float fraction, float target, float step);

// A list control: its items in order and the focused entry.
struct MenuList {
  std::vector<MenuItem> items;
  int focus = -1;
  // A grid (PAINT CAR's colour swatches, VINYL SELECT): its items are the cells on screen, column by
  // column, this many to a column; an item's index is its cell's. 0: a plain list.
  int grid_rows = 0;
};

inline int EntryOf(const MenuList& list, int item) {
  if (item < 0 || item >= int(list.items.size())) return -1;
  return list.items[size_t(item)].index >= 0 ? list.items[size_t(item)].index : item;
}

// Whether a list is one row of items side by side (the main menu's tiles, the upgrade shop's strip): the
// wheel moves along it (up = left), as a PC's does on a sideways strip. A grid is not one.
bool IsRow(const MenuList& list);

// The shown item under the point, or -1. An item's area along the list's axis is at most half the
// distance to its neighbours each way, so rows whose drawings overlap still split at the midpoint.
int ItemAt(const MenuList& list, UiPoint point);

// A button prompt (SELECT, BACK...): its icon's centre, the icon's size and its label's width, as the
// prompt bar lays them out; the clickable area is the icon, the gap and the label.
struct Prompt {
  float x = 0, y = 0;  // the icon's centre
  float icon = 20, label = 0;
  uint32_t buttons = 0;  // PromptButtons()
  bool icon_hidden = false;  // the mouse in use: only the label is shown, and only it takes the click
};
bool PromptHit(const Prompt& prompt, UiPoint point);
// A box in UI space (y up).
struct UiRect {
  float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
  bool tile = false;  // the pointer's highlight lights the whole box (a carousel's tile) instead of a line under it
  bool Contains(UiPoint p) const { return p.x >= min_x && p.x <= max_x && p.y >= min_y && p.y <= max_y; }
  bool operator==(const UiRect&) const = default;
};
// The prompt's clickable box (PromptHit), which the pointer's highlight covers too.
UiRect PromptBox(const Prompt& prompt);
// Whether a click on an item of the list chooses it (A): not when every shown item is a value to step or
// slide (ASSISTS & DIFFICULTY, TUNE SETUP): a click there changes the value, and OK is the prompt's.
bool ClickChooses(const MenuList& list);
// Whether a click on a prompt presses its button: all do, but for the bottom row's SELECT where a click on the item
// itself chooses (its label is gone with the mouse in use).
bool PromptTakesClick(const Prompt& prompt, bool clicks_choose);
// A page of values (sliders, spinners) on screen, nothing to choose: its BACK can throw changes away. A list still
// fading in, with nothing shown yet, is not one.
bool ValuesOnly(const MenuList& list);
// Its items run across the screen (a carousel of tiles), not down it.
bool HorizontalList(const MenuList& list);

// What a prompt's icon (its texture path, "...\UI4\A_Button_F3.tga") stands for: X_INPUT_GAMEPAD_* bits,
// kLeftTriggerBit / kRightTriggerBit for the triggers; 0 for icons that are no single button (the D-pad).
inline constexpr uint32_t kLeftTriggerBit = 0x10000, kRightTriggerBit = 0x20000;
uint32_t PromptButtons(std::string_view icon_path);

// A tab header (TUNE SETUP's TIRES | GEARING | ALIGNMENT): a row of three text items, the previous,
// current and next tab's names (an end slot is empty on the first and last tab). Clicking the previous
// name, or anywhere left of it on the header's row (the Q arrow), is -1 (LB); the next name or right of it
// (the E arrow) is +1 (RB); 0 elsewhere.
bool IsTabHeader(const MenuList& list);
int TabClick(const MenuList& list, UiPoint point);

// Which controls take the mouse now. A screen's lists are on its layer; its prompt bar runs along the
// bottom row, often from an overlay layer above the lists. A dialog's prompts are off the bottom row, at
// the bottom right of its box (kDialogWidth wide, centred): while one is up, only its prompts take clicks
// and no list follows the pointer - unless the dialog holds a list of its own (CREATE LAYER), drawn
// inside its box (above its prompts): then that list and the dialog's prompts take the mouse, and nothing
// behind the dialog does. (A dialog's layer can be above, equal to or even below the menu it covers.) `frame`
// is the frame the control's scene was last drawn (stale scenes are ignored).
struct Candidate {
  bool list = false;   // a focused list with shown items; else a prompt bar with laid-out prompts
  int layer = -1;
  uint32_t frame = 0;
  bool above_bottom_row = false;  // a bar with a prompt above the bottom row (a dialog's)
  float min_x = 0, max_x = 0;     // a list: its shown items' extent; a dialog's bar: its box (UI x)
  float min_y = 0;                // a list: its lowest shown item's bottom; a dialog's bar: its prompts' row
};
inline constexpr float kDialogWidth = 700;
// A dialog's box from its prompts: the right end of the rightmost one is the box's right edge.
// `bottom` (optional): the prompts' row (UI y), the box's bottom edge.
void DialogSpan(const std::vector<Prompt>& prompts, float& left, float& right, float* bottom = nullptr);
inline constexpr float kBottomRowY = -290;  // the screen's own prompts sit below this (UI y)
struct Active {
  std::vector<bool> active;  // per candidate
  bool modal = false;        // a dialog is up
};
Active ChooseActive(const std::vector<Candidate>& candidates);

}  // namespace fm4_static_recomp::input
