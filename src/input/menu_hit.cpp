// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "input/menu_hit.h"

#include <set>
#include <map>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace fm4_static_recomp::input {
namespace {

constexpr float kUiWidth = 1280, kUiHeight = 720;

float CentreX(const MenuItem& item) { return (item.min_x + item.max_x) / 2; }
float CentreY(const MenuItem& item) { return (item.min_y + item.max_y) / 2; }

// A row's centre along a column: its bounds' centre, or, when its drawings reach much further than the rows'
// spacing, half that spacing below where the row itself sits (its anchor, the top of the row).
float RowCentre(const MenuItem& item, float spacing) {
  if (spacing > 0 && std::isfinite(item.anchor_y) && item.max_y - item.min_y > 1.5f * spacing) {
    return item.anchor_y - spacing / 2;
  }
  return CentreY(item);
}

// The spacing of a column's rows: the smallest gap between neighbouring shown rows' anchors, else centres.
float RowSpacing(const MenuList& list) {
  float spacing = 0;
  const MenuItem* last = nullptr;
  for (const MenuItem& item : list.items) {
    if (!item.shown) continue;
    if (last) {
      const bool anchors = std::isfinite(item.anchor_y) && std::isfinite(last->anchor_y);
      const float d = std::fabs(anchors ? item.anchor_y - last->anchor_y : CentreY(item) - CentreY(*last));
      if (d > 0 && (spacing == 0 || d < spacing)) spacing = d;
    }
    last = &item;
  }
  return spacing;
}

// Whether the shown items run left to right (a row) rather than top to bottom.
bool Horizontal(const MenuList& list) {
  const MenuItem* first = nullptr;
  for (const MenuItem& item : list.items) {
    if (!item.shown) continue;
    if (!first) {
      first = &item;
      continue;
    }
    return std::fabs(CentreX(item) - CentreX(*first)) > std::fabs(CentreY(item) - CentreY(*first));
  }
  return false;
}

}  // namespace

bool IsRow(const MenuList& list) { return list.grid_rows == 0 && Horizontal(list); }

ContentRect PictureRect(float window_width, float window_height, bool letterbox) {
  if (!letterbox || window_width <= 0 || window_height <= 0) return {0, 0, window_width, window_height};
  if (window_width * 9 > window_height * 16) {
    const float width = window_height * 16 / 9;
    return {(window_width - width) / 2, 0, width, window_height};
  }
  const float height = window_width * 9 / 16;
  return {0, (window_height - height) / 2, window_width, height};
}

std::optional<UiPoint> ToUi(float x, float y, const ContentRect& picture) {
  if (picture.width <= 0 || picture.height <= 0) return std::nullopt;
  const float u = (x - picture.left) / picture.width, v = (y - picture.top) / picture.height;
  if (u < 0 || u > 1 || v < 0 || v > 1) return std::nullopt;
  return UiPoint{u * kUiWidth - kUiWidth / 2, kUiHeight / 2 - v * kUiHeight};
}

bool HorizontalList(const MenuList& list) { return Horizontal(list); }

int ItemAt(const MenuList& list, UiPoint point) {
  // A grid's cells are laid edge to edge: the one the point is in.
  if (list.grid_rows > 0) {
    for (int i = 0; i < int(list.items.size()); ++i) {
      const MenuItem& item = list.items[size_t(i)];
      if (item.shown && point.x >= item.min_x && point.x < item.max_x && point.y > item.min_y &&
          point.y <= item.max_y) {
        return i;
      }
    }
    return -1;
  }
  const bool horizontal = Horizontal(list);
  const int count = int(list.items.size());
  // A column's rows are one width, the widest row's, with a margin: pointing anywhere along a row
  // (its label, its slider, its value) picks it. Labels set to the left of a row's start (AUDIO's, right-aligned
  // text the measure has no width for) reach further left: the longest is about 200 units.
  constexpr float kRowMargin = 48, kLabelReach = 220;
  float row_left = 0, row_right = 0;
  bool any = false;
  for (const MenuItem& item : list.items) {
    if (!item.shown) continue;
    row_left = any ? std::min(row_left, item.min_x) : item.min_x;
    row_right = any ? std::max(row_right, item.max_x) : item.max_x;
    any = true;
  }
  const float spacing = horizontal ? 0 : RowSpacing(list);
  const auto centre_of = [&](const MenuItem& item) { return horizontal ? CentreX(item) : RowCentre(item, spacing); };
  for (int i = 0; i < count; ++i) {
    const MenuItem& item = list.items[size_t(i)];
    if (!item.shown) continue;
    float lo = horizontal ? item.min_x : item.min_y, hi = horizontal ? item.max_x : item.max_y;
    const float centre = centre_of(item);
    // At most half the pitch to the nearest shown neighbour each way (the end items too), when that
    // is nearer than the item's own edge: drawings that overlap the next row don't count.
    float pitch = 0;
    for (int step : {-1, 1}) {
      for (int j = i + step; j >= 0 && j < count; j += step) {
        const MenuItem& other = list.items[size_t(j)];
        if (!other.shown) continue;
        const float d = std::fabs(centre - centre_of(other));
        if (d > 0 && (pitch == 0 || d < pitch)) pitch = d;
        break;
      }
    }
    if (pitch > 0) {
      lo = std::max(lo, centre - pitch / 2);
      hi = std::min(hi, centre + pitch / 2);
    }
    const float along = horizontal ? point.x : point.y, across = horizontal ? point.y : point.x;
    const float across_lo = horizontal ? item.min_y : row_left - kLabelReach;
    const float across_hi = horizontal ? item.max_y : row_right + kRowMargin;
    if (along >= lo && along <= hi && across >= across_lo && across <= across_hi) return i;
  }
  return -1;
}

int ListUnder(const std::vector<const MenuList*>& lists, UiPoint point) {
  int first = -1;
  for (int i = 0; i < int(lists.size()); ++i) {
    if (!lists[size_t(i)] || ItemAt(*lists[size_t(i)], point) < 0) continue;
    if (first < 0) first = i;
    const auto& items = lists[size_t(i)]->items;
    if (std::any_of(items.begin(), items.end(), [&](const MenuItem& item) {
          return item.shown && point.x >= item.min_x && point.x <= item.max_x;
        })) {
      return i;
    }
  }
  return first;
}

UiRect PromptBox(const Prompt& prompt) {
  // As the bar lays them out: the label's element starts 10 units after the icon's edge.
  constexpr float kPad = 8, kLabelGap = 10;
  const float half = prompt.icon / 2, text = prompt.x + half + kLabelGap;
  const float left = prompt.icon_hidden ? text - kPad : prompt.x - half - kPad;
  return {left, prompt.y - half - kPad, text + prompt.label + kPad, prompt.y + half + kPad};
}

bool PromptHit(const Prompt& prompt, UiPoint point) { return PromptBox(prompt).Contains(point); }

bool ClickChooses(const MenuList& list) {
  return std::any_of(list.items.begin(), list.items.end(),
                     [](const MenuItem& item) { return item.shown && !item.slider && !item.spinner; });
}

bool ValuesOnly(const MenuList& list) {
  const auto shown_value = [](const MenuItem& item) { return item.shown && (item.slider || item.spinner); };
  return std::any_of(list.items.begin(), list.items.end(), shown_value) && !ClickChooses(list);
}

bool PromptTakesClick(const Prompt& prompt, bool clicks_choose) {
  constexpr uint32_t kA = 0x1000;
  if (prompt.y > kBottomRowY) return true;  // a dialog's
  return prompt.buttons != kA || !clicks_choose;  // SELECT is the click on the item itself
}

uint32_t PromptButtons(std::string_view icon_path) {
  const size_t slash = icon_path.find_last_of("\\/");
  const std::string_view name = slash == std::string_view::npos ? icon_path : icon_path.substr(slash + 1);
  struct Entry {
    std::string_view prefix;
    uint32_t buttons;
  };
  // Longest first where one name starts another (Back_Button_D before Back_Button).
  static constexpr Entry kEntries[] = {
      {"A_Button", 0x1000},   {"B_Button", 0x2000},       {"X_Button", 0x4000},       {"Y_Button", 0x8000},
      {"Start_Button", 0x10}, {"Back_Button", 0x20},      {"LBumper", 0x100},         {"RBumper", 0x200},
      {"L_Thumbstick", 0x40}, {"RThumbstick", 0x80},      {"LTrigger", kLeftTriggerBit},
      {"RTrigger", kRightTriggerBit},
  };
  for (const Entry& entry : kEntries) {
    if (name.substr(0, entry.prefix.size()) == entry.prefix) return entry.buttons;
  }
  return 0;
}

bool IsTabHeader(const MenuList& list) {
  if (list.items.size() != 3) return false;
  int texts = 0;
  for (const MenuItem& item : list.items) {
    if (!item.shown) continue;
    if (item.max_x - item.min_x > 1 || item.max_y - item.min_y > 1) return false;  // text anchors only
    ++texts;
  }
  if (texts < 2) return false;
  const MenuItem& a = list.items[0].shown ? list.items[0] : list.items[1];
  const MenuItem& b = list.items[2].shown ? list.items[2] : list.items[1];
  return std::fabs(a.min_y - b.min_y) < 1 && b.min_x > a.min_x;  // one row, left to right
}

int TabClick(const MenuList& list, UiPoint point) {
  if (!IsTabHeader(list)) return 0;
  constexpr float kHalfHeight = 18;
  const MenuItem& current = list.items[1];
  const float y = current.shown ? current.min_y : (list.items[0].shown ? list.items[0].min_y : list.items[2].min_y);
  if (std::fabs(point.y - y) > kHalfHeight) return 0;
  // The slots are evenly spaced; the current tab is the middle one. A neighbour's side of the row (past half
  // way from the current name, when its slot has a name: names grow away from their anchors) or the arrow
  // beyond it changes tab; the current name does nothing.
  const float centre = current.min_x;
  const MenuItem& known = list.items[2].shown ? list.items[2] : list.items[0];
  const float spacing = std::fabs(known.min_x - centre);
  if (spacing < 1) return 0;
  if (point.x < centre - 1.5f * spacing) return -1;  // the Q arrow
  if (point.x > centre + 1.5f * spacing) return 1;   // the E arrow
  if (list.items[0].shown && point.x < centre - spacing / 2) return -1;
  if (list.items[2].shown && point.x > centre + spacing / 2) return 1;
  return 0;
}

int SpinnerClick(const MenuItem& item, float x) {
  // The arrows are icons, whose bounds are the game's 100-wide icon box: measured on AUDIO, the left arrow's
  // centre is 64 in from the row's right end, the right arrow's 50. The left arrow's half takes -1.
  constexpr float kLeftCentre = 64, kRightCentre = 50;
  const float split = item.max_x - (kLeftCentre + kRightCentre) / 2;
  if (x >= item.max_x - kLeftCentre - 10 && x < split) return -1;
  return 1;
}

bool OnSliderBar(const MenuItem& item, UiPoint point) {
  constexpr float kSlack = 12;
  return item.slider && item.bar_right > item.bar_left && point.x >= item.bar_left - kSlack &&
         point.x <= item.bar_right + kSlack;
}

float SliderTarget(const MenuItem& item, float x) {
  if (item.bar_right <= item.bar_left) return item.fraction;
  return std::clamp((x - item.bar_left) / (item.bar_right - item.bar_left), 0.0f, 1.0f);
}

int SliderStep(float fraction, float target, float step) {
  const float half = std::max(step, 0.001f) / 2;
  if (target > fraction + half) return 1;
  if (target < fraction - half) return -1;
  return 0;
}

Active ChooseActive(const std::vector<Candidate>& candidates) {
  Active out;
  out.active.assign(candidates.size(), false);
  uint32_t newest = 0;
  for (const Candidate& c : candidates) newest = std::max(newest, c.frame);
  const auto current = [newest](const Candidate& c) { return c.frame != 0 && c.frame + 2 >= newest; };
  int list_top = -1;
  for (const Candidate& c : candidates) {
    if (c.list && current(c)) list_top = std::max(list_top, c.layer);
  }
  // A list inside a dialog's box is the dialog's own. (The layer byte is no stacking order: a message box
  // can have a lower one than the menu it covers.)
  constexpr float kSlack = 24;
  const auto inside = [&](const Candidate& list, const Candidate& bar) {
    return list.list && current(list) && list.min_x >= bar.min_x - kSlack && list.max_x <= bar.max_x + kSlack &&
           list.min_y >= bar.min_y;
  };
  const auto dialog_bar = [&](const Candidate& c) { return !c.list && current(c) && c.above_bottom_row; };
  const auto holds_list = [&](const Candidate& bar) {
    return std::any_of(candidates.begin(), candidates.end(), [&](const Candidate& l) { return inside(l, bar); });
  };
  bool dialog_list = false;
  for (const Candidate& c : candidates) {
    if (!dialog_bar(c)) continue;
    if (holds_list(c)) {
      dialog_list = true;
    } else {
      out.modal = true;
    }
  }
  for (size_t i = 0; i < candidates.size(); ++i) {
    const Candidate& c = candidates[i];
    if (!current(c)) continue;
    if (out.modal) {
      out.active[i] = dialog_bar(c) && !holds_list(c);
    } else if (dialog_list) {
      // Only the dialog: its prompts and the lists in its box.
      out.active[i] = c.list ? std::any_of(candidates.begin(), candidates.end(),
                                           [&](const Candidate& b) { return dialog_bar(b) && inside(c, b); })
                             : dialog_bar(c);
    } else {
      out.active[i] = c.list ? c.layer == list_top : true;
    }
  }
  if (!out.modal && !dialog_list) {
    // The layer byte is no stacking order between screens either (PARTY LOBBY can sit on a higher one than the
    // FIND RACE opened over it): of the current screens, one whose list overlaps a list of a screen opened later
    // is covered by it, whatever their layers, and the screen covering it takes its place.
    std::map<uint32_t, uint64_t> opened;
    for (const Candidate& c : candidates) {
      if (c.list && current(c) && c.screen) opened[c.screen] = std::max(opened[c.screen], c.opened);
    }
    std::set<uint32_t> covered, covering;
    constexpr float kOverlap = 8;
    for (const Candidate& a : candidates) {
      if (!a.list || !current(a) || !a.screen) continue;
      for (const Candidate& b : candidates) {
        if (!b.list || !current(b) || !b.screen || a.screen == b.screen) continue;
        if (opened[a.screen] < opened[b.screen] && std::min(a.max_x, b.max_x) - std::max(a.min_x, b.min_x) > kOverlap &&
            std::min(a.max_y, b.max_y) - std::max(a.min_y, b.min_y) > kOverlap) {
          covered.insert(a.screen);
          covering.insert(b.screen);
        }
      }
    }
    for (size_t i = 0; i < candidates.size(); ++i) {
      const Candidate& c = candidates[i];
      if (!current(c) || !c.screen) continue;
      if (covered.contains(c.screen)) {
        out.active[i] = false;  // its lists and its bottom bar: nothing of it is drawn any more
      } else if (c.list && covering.contains(c.screen)) {
        out.active[i] = true;
      }
    }
  }
  return out;
}

void DialogSpan(const std::vector<Prompt>& prompts, float& left, float& right, float* bottom) {
  right = 0;
  bool any = false;
  for (const Prompt& p : prompts) {
    if (p.y <= kBottomRowY) continue;
    const float end = p.x + p.icon / 2 + p.label;
    right = any ? std::max(right, end) : end;
    if (bottom) *bottom = any ? std::min(*bottom, p.y) : p.y;
    any = true;
  }
  left = any ? right - kDialogWidth : 0;
  if (!any) right = 0;
}

}  // namespace fm4_static_recomp::input
