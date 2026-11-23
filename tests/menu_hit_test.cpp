// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "input/menu_hit.h"

using namespace fm4_static_recomp::input;
using Catch::Approx;

namespace {

// The main menu's five tiles as the game lays them out (measured live).
MenuList MainMenu() {
  MenuList list;
  for (int i = 0; i < 5; ++i) {
    const float left = -460 + 186.0f * i;
    list.items.push_back({true, left, -235, left + 176, 137});
  }
  list.focus = 0;
  return list;
}

// The career side list: rows 40 apart whose drawings are 142 tall; five shown, five hidden.
MenuList Career() {
  MenuList list;
  for (int i = 0; i < 10; ++i) {
    const float top = 360 - 40.0f * i;
    list.items.push_back({i < 5, -584, top - 142, -238, top});
  }
  list.focus = 0;
  return list;
}

}  // namespace

TEST_CASE("Window pixels map to the game's UI space, letterbox bars outside", "[menu_hit]") {
  const ContentRect full = PictureRect(1920, 1080, true);
  CHECK(full.left == 0);
  CHECK(full.width == 1920);
  auto centre = ToUi(960, 540, full);
  REQUIRE(centre);
  CHECK(centre->x == Approx(0));
  CHECK(centre->y == Approx(0));
  auto top_left = ToUi(0, 0, full);
  CHECK(top_left->x == Approx(-640));
  CHECK(top_left->y == Approx(360));
  // A 4:3 window letterboxed: bars above and below, nothing there.
  const ContentRect boxed = PictureRect(1600, 1200, true);
  CHECK(boxed.top == Approx(150));
  CHECK_FALSE(ToUi(800, 100, boxed));
  CHECK(ToUi(800, 600, boxed)->y == Approx(0));
  // Stretched when the presenter does not letterbox.
  CHECK(PictureRect(1600, 1200, false).height == 1200);
}

TEST_CASE("The main menu's tiles are hit by their drawn bounds, the gaps hit nothing", "[menu_hit]") {
  const MenuList list = MainMenu();
  CHECK(ItemAt(list, {-372, 0}) == 0);   // PLAY CAREER's middle
  CHECK(ItemAt(list, {-186, 100}) == 1);
  CHECK(ItemAt(list, {372, -200}) == 4);
  CHECK(ItemAt(list, {-279, 0}) == -1);  // between tiles 0 and 1 (gap 284..274 is 10 wide)
  CHECK(ItemAt(list, {0, 300}) == -1);   // above the tiles
}

TEST_CASE("Rows whose drawings overlap split at the midpoint, hidden items never hit", "[menu_hit]") {
  const MenuList list = Career();
  // Row centres: 289, 249, 209, 169, 129.
  CHECK(ItemAt(list, {-400, 289}) == 0);
  CHECK(ItemAt(list, {-400, 270}) == 0);
  CHECK(ItemAt(list, {-400, 268}) == 1);
  CHECK(ItemAt(list, {-400, 129}) == 4);
  CHECK(ItemAt(list, {-400, 60}) == -1);  // where the hidden items are drawn: nothing
  CHECK(ItemAt(list, {0, 289}) == -1);    // beside the list
  CHECK(ItemAt(list, {-620, 289}) == 0);  // just left of the row's drawing: still the row
  CHECK(ItemAt(list, {-200, 249}) == 1);  // just right of it
}

TEST_CASE("A scrolling carousel: an item shows an entry, the focus counts entries", "[menu_hit]") {
  // PAINT CAR: nine items 220 apart; the five categories are shown by items 4..8 (item 4 = entry 0, the
  // focused one, in the middle); items 0..3 are empty slots.
  MenuList paint;
  for (int i = 0; i < 9; ++i) {
    const float x = -855 + 220.0f * i;
    MenuItem item{i >= 4, x - 88, -280, x + 88, -120};
    item.index = i >= 4 ? i - 4 : -1;
    paint.items.push_back(item);
  }
  paint.focus = 0;
  CHECK(EntryOf(paint, 6) == 2);
  CHECK(EntryOf(paint, 4) == 0);
  CHECK(EntryOf(paint, 1) == 1);  // an item with no entry stands for its own place
  CHECK(EntryOf(paint, 9) == -1);
  CHECK(ItemAt(paint, {-855 + 220.0f * 6, -200}) == 6);
  CHECK(ItemAt(paint, {-855 + 220.0f * 2, -200}) == -1);  // an empty slot is not shown
}

TEST_CASE("Prompts are clicked on their icon or their label", "[menu_hit]") {
  // The career screen's SELECT and BACK, as the prompt bar lays them out.
  const Prompt select{-506, -313, 20, 48, 0x1000};
  const Prompt back{-408, -313, 20, 34.5f, 0x2000};
  CHECK(PromptHit(select, {-506, -313}));  // the icon
  CHECK(PromptHit(select, {-470, -310}));  // the label
  CHECK_FALSE(PromptHit(select, {-428, -313}));  // between SELECT and BACK
  CHECK(PromptHit(back, {-380, -316}));
  CHECK_FALSE(PromptHit(back, {-380, -345}));  // below the bar
  CHECK(PromptHit(select, {-521, -313}));  // just left of the icon (it is drawn a little right of centre)
}

TEST_CASE("The screen's SELECT and BACK leave the click to the mouse's buttons; a dialog's take it", "[menu_hit]") {
  Prompt select{.x = -506, .y = -313, .icon = 20, .label = 48, .buttons = 0x1000};
  Prompt back{.x = -400, .y = -313, .icon = 20, .label = 34, .buttons = 0x2000};
  Prompt rename{.x = -300, .y = -313, .icon = 20, .label = 60, .buttons = 0x4000};
  CHECK_FALSE(PromptTakesClick(select, true));
  CHECK(PromptTakesClick(select, false));  // nothing else to click on the screen
  CHECK(PromptTakesClick(back, true));  // BACK is a button of its own
  CHECK(PromptTakesClick(back, false));
  CHECK(PromptTakesClick(rename, true));
  Prompt ok{.x = 300, .y = -40, .icon = 20, .label = 20, .buttons = 0x1000};
  Prompt cancel{.x = 380, .y = -40, .icon = 20, .label = 50, .buttons = 0x2000};
  CHECK(PromptTakesClick(ok, true));
  CHECK(PromptTakesClick(cancel, true));
}

TEST_CASE("A click chooses an item, unless every item is a value to step or slide", "[menu_hit]") {
  MenuItem row;
  row.shown = true;
  MenuItem spinner = row, slider = row, hidden;
  spinner.spinner = true;
  slider.slider = true;
  CHECK(ClickChooses(MenuList{.items = {row, spinner}}));
  CHECK_FALSE(ClickChooses(MenuList{.items = {spinner, slider, hidden}}));
  CHECK_FALSE(ClickChooses(MenuList{}));
  CHECK(ValuesOnly(MenuList{.items = {spinner, slider, hidden}}));
  CHECK_FALSE(ValuesOnly(MenuList{.items = {row, spinner}}));
  CHECK_FALSE(ValuesOnly(MenuList{.items = {hidden}}));  // still fading in
  CHECK_FALSE(ValuesOnly(MenuList{}));
}

TEST_CASE("Rows whose drawings reach far past them are placed where they sit (AUDIO)", "[menu_hit]") {
  // AUDIO's choices as the game lays them out: each row 35 apart (anchors 92 .. -48), drawings 133 tall that
  // reach down past the row (hidden arrows); the last one reaches furthest.
  MenuList list;
  const float anchors[] = {92, 57, 22, -13, -48};
  const float bottoms[] = {8, -27, -62, -97, -132};
  for (int i = 0; i < 5; ++i) {
    MenuItem row;
    row.shown = true;
    row.min_x = -100, row.max_x = 374, row.min_y = bottoms[i], row.max_y = bottoms[i] + 133;
    row.anchor_y = anchors[i];
    list.items.push_back(row);
  }
  CHECK(ItemAt(list, {0, 77}) == 0);    // on the first row's text
  CHECK(ItemAt(list, {0, -27}) == 3);   // on the fourth (MENU MUSIC)
  CHECK(ItemAt(list, {0, -62}) == 4);   // on the last (PLAYBACK SYSTEM), not the one above
  CHECK(ItemAt(list, {0, -90}) == -1);  // below the last row: nothing
  CHECK(ItemAt(list, {-256, 77}) == 0);  // on the row's label, right-aligned left of the row's start
}

TEST_CASE("A carousel of tiles runs across, a column of rows does not", "[menu_hit]") {
  const auto at = [](float x, float y) {
    MenuItem item;
    item.shown = true;
    item.min_x = x, item.max_x = x + 190, item.min_y = y, item.max_y = y + 140;
    return item;
  };
  CHECK(HorizontalList(MenuList{.items = {at(-100, -238), at(120, -238), at(340, -238)}}));
  CHECK_FALSE(HorizontalList(MenuList{.items = {at(-584, 200), at(-584, 160), at(-584, 120)}}));
}

TEST_CASE("A row of tiles is a row; a column of rows and a grid are not", "[menu_hit]") {
  auto item = [](float x0, float y0, float x1, float y1) {
    MenuItem it;
    it.min_x = x0, it.min_y = y0, it.max_x = x1, it.max_y = y1, it.shown = true;
    return it;
  };
  MenuList tiles{.items = {item(-460, -264, -284, 137), item(-274, -264, -98, 137), item(-88, -264, 88, 137)}};
  CHECK(IsRow(tiles));
  MenuList rows{.items = {item(-560, 280, -400, 300), item(-560, 250, -400, 270), item(-560, 220, -400, 240)}};
  CHECK_FALSE(IsRow(rows));
  MenuList grid = tiles;
  grid.grid_rows = 3;
  CHECK_FALSE(IsRow(grid));
  CHECK_FALSE(IsRow(MenuList{}));
}

TEST_CASE("A prompt's icon names its button", "[menu_hit]") {
  CHECK(PromptButtons("GAME:\\Media\\UI\\Textures\\UI4\\A_Button_F3.tga") == 0x1000);
  CHECK(PromptButtons("GAME:\\Media\\UI\\Textures\\UI4\\B_Button_F3.tga") == 0x2000);
  CHECK(PromptButtons("UI4/Y_Button_F3.tga") == 0x8000);
  CHECK(PromptButtons("Back_Button_D_F3.tga") == 0x20);
  CHECK(PromptButtons("Start_Button_F3.tga") == 0x10);
  CHECK(PromptButtons("RBumper_I.tga") == 0x200);
  CHECK(PromptButtons("LTrigger_F3.tga") == kLeftTriggerBit);
  CHECK(PromptButtons("D_Pad_Button_F3.tga") == 0);
}

TEST_CASE("The screen's lists and prompts take the mouse; a dialog takes it alone", "[menu_hit]") {
  // Manufacturer select: the list on 6A, its prompt bar on the overlay layer 6B along the bottom row.
  {
    const Active a = ChooseActive({{true, 0x6A, 100, false}, {false, 0x6B, 100, false}});
    CHECK_FALSE(a.modal);
    CHECK(a.active[0]);
    CHECK(a.active[1]);
  }
  // The main menu with FEATURE UNAVAILABLE up: the dialog's OK is mid-screen, one layer up.
  {
    const Active a = ChooseActive({{true, 0x6A, 100, false, -560, 560}, {false, 0x6A, 100, false},
                                   {false, 0x6B, 100, true, -370, 330}});
    CHECK(a.modal);
    CHECK_FALSE(a.active[0]);  // the tiles under the dialog
    CHECK_FALSE(a.active[1]);  // the main menu's own SELECT / BACK
    CHECK(a.active[2]);
  }
  // The career menu with FEATURE UNAVAILABLE up, on a lower layer than the menu: the list is outside the box.
  {
    Candidate career{true, 0x6B, 100, false, -529, -284};
    Candidate career_bar{false, 0x6B, 100, false};
    Candidate box{false, 0x6A, 100, true, -370, 330};
    const Active a = ChooseActive({career, career_bar, box});
    CHECK(a.modal);
    CHECK_FALSE(a.active[0]);
    CHECK(a.active[2]);
  }
  {
    Candidate career{true, 0x6A, 100, false, -529, -284};
    Candidate career_bar{false, 0x6A, 100, false};
    Candidate box{false, 0x6A, 100, true, -370, 330};
    const Active a = ChooseActive({career, career_bar, box});
    CHECK(a.modal);
    CHECK_FALSE(a.active[0]);
    CHECK_FALSE(a.active[1]);
    CHECK(a.active[2]);
  }
  // CREATE LAYER: a dialog holding a list (inside its box, above its prompts) is no wall: its list takes the
  // mouse. Nothing behind it does: the layer strip (same layer, wider than the box), the editor's tab row
  // (inside the box's width but below its prompts) and the editor's own prompts.
  {
    Candidate strip{true, 0x6A, 100, false, -246, 786, -340};
    Candidate tabs{true, 0x6A, 100, false, -160, 160, -188};
    Candidate create{true, 0x6A, 100, false, -320, 29, -140};
    Candidate box{false, 0x6A, 100, true, -370, 330, -169};
    Candidate editor_bar{false, 0x6A, 100, false};
    const Active a = ChooseActive({strip, tabs, create, box, editor_bar});
    CHECK_FALSE(a.modal);
    CHECK_FALSE(a.active[0]);
    CHECK_FALSE(a.active[1]);
    CHECK(a.active[2]);
    CHECK(a.active[3]);
    CHECK_FALSE(a.active[4]);
  }
  // A list left from an earlier screen (an older frame) does not count, nor does a lower list.
  {
    const Active a = ChooseActive({{true, 0x6A, 90, false}, {true, 0x6B, 100, false}, {true, 0x69, 100, false}});
    CHECK_FALSE(a.active[0]);
    CHECK(a.active[1]);
    CHECK_FALSE(a.active[2]);
  }
}

TEST_CASE("TUNE SETUP's tab header: the neighbours' names and the arrows change tab", "[menu_hit]") {
  // TIRES (the first tab): an empty slot left, TIRES in the middle, GEARING right; text anchors only.
  MenuList tabs;
  tabs.items = {{false, -134, 290, -134, 290}, {true, 26, 290, 26, 290}, {true, 186, 290, 186, 290}};
  CHECK(IsTabHeader(tabs));
  CHECK(TabClick(tabs, {190, 292}) == 1);    // GEARING
  CHECK(TabClick(tabs, {560, 290}) == 1);    // the E arrow, far right on the row
  CHECK(TabClick(tabs, {30, 290}) == 0);     // TIRES itself
  CHECK(TabClick(tabs, {-134, 290}) == 0);   // the empty slot: no tab before TIRES
  CHECK(TabClick(tabs, {-560, 290}) == -1);  // the Q arrow still asks (the game decides)
  CHECK(TabClick(tabs, {190, 200}) == 0);    // below the header
  // GEARING: TIRES left, ALIGNMENT right.
  tabs.items[0].shown = true;
  CHECK(TabClick(tabs, {-130, 288}) == -1);
  // The livery editor's sides: the names grow away from their anchors (TOP/HOOD ends at its anchor, REAR
  // BUMPER starts at its), so the whole of a long name takes the click, out to the arrows.
  MenuList sides;
  sides.items = {{true, -160, -188, -160, -188}, {true, 0, -188, 0, -188}, {true, 160, -188, 160, -188}};
  CHECK(TabClick(sides, {250, -190}) == 1);    // REAR BUMPER's far end
  CHECK(TabClick(sides, {-222, -190}) == -1);  // TOP/HOOD's far end
  CHECK(TabClick(sides, {95, -190}) == 1);     // past half way to REAR BUMPER
  CHECK(TabClick(sides, {40, -190}) == 0);     // RIGHT SIDE itself
  CHECK(TabClick(sides, {-40, -190}) == 0);
  // A list of boxes is not a tab header.
  MenuList rows;
  rows.items = {{true, -100, 0, 100, 30}, {true, -100, -40, 100, -10}, {true, -100, -80, 100, -50}};
  CHECK_FALSE(IsTabHeader(rows));
}

TEST_CASE("A prompt's box covers its icon, the gap and its label, with a margin", "[menu_hit]") {
  Prompt select;
  select.x = -506, select.y = -313, select.icon = 20, select.label = 48;
  const UiRect box = PromptBox(select);
  CHECK(box.min_x == Approx(-524));
  CHECK(box.max_x == Approx(-506 + 10 + 10 + 48 + 8));
  CHECK(box.min_y == Approx(-331));
  CHECK(box.max_y == Approx(-295));
  CHECK(box.Contains({-470, -313}));
  CHECK_FALSE(box.Contains({-425, -313}));
  CHECK(PromptHit(select, {-470, -313}));
  // The icon hidden (the mouse in use): the label alone.
  select.icon_hidden = true;
  CHECK(PromptBox(select).min_x == Approx(-506 + 10 + 10 - 8));
  // Centred on the text: it runs from 20 after the anchor for the label's width.
  CHECK((PromptBox(select).min_x + PromptBox(select).max_x) / 2 == Approx(-506 + 20 + 48 / 2.0f));
  CHECK_FALSE(PromptHit(select, {-510, -313}));
  CHECK(PromptHit(select, {-470, -313}));
}

TEST_CASE("Dragging a slider: the pointer's place on the bar is the target, steps stop within half a step", "[menu_hit]") {
  // FRONT tire pressure: bar from -208 to 138, value at 0.4444 (steps of 1/45).
  MenuItem front;
  front.shown = true;
  front.slider = true;
  front.bar_left = -208, front.bar_right = 138, front.fraction = 0.4444f;
  CHECK(OnSliderBar(front, {0, 175}));
  CHECK(OnSliderBar(front, {-215, 175}));  // just past the left end
  CHECK_FALSE(OnSliderBar(front, {-300, 175}));  // the label
  CHECK(SliderTarget(front, -208) == Approx(0));
  CHECK(SliderTarget(front, 138) == Approx(1));
  CHECK(SliderTarget(front, 500) == Approx(1));  // dragged past the end: the end
  CHECK(SliderTarget(front, -35) == Approx(0.5));
  const float step = 1.0f / 45;
  CHECK(SliderStep(0.4444f, 0.5f, step) == 1);
  CHECK(SliderStep(0.4444f, 0.30f, step) == -1);
  CHECK(SliderStep(0.4444f, 0.4500f, step) == 0);  // within half a step: stays
  MenuItem row;  // not a slider
  CHECK_FALSE(OnSliderBar(row, {0, 0}));
}

TEST_CASE("A spinner's left arrow steps back, its right arrow or the row steps on", "[menu_hit]") {
  MenuItem music;  // IN RACE MUSIC (measured): the row ends at 374, its arrows at 310 and 324
  music.shown = true, music.spinner = true;
  music.min_x = -523, music.max_x = 374;
  CHECK(SpinnerClick(music, 310) == -1);
  CHECK(SpinnerClick(music, 305) == -1);
  CHECK(SpinnerClick(music, 324) == 1);
  CHECK(SpinnerClick(music, -100) == 1);  // the value itself
  CHECK(SpinnerClick(music, -400) == 1);  // the label
}

namespace {
// PAINT CAR's colour swatches: 22 columns of 9 cells, 45 x 12, column by column from the top left
// (-494, -173).
MenuList Swatches() {
  MenuList grid;
  grid.grid_rows = 9;
  for (int i = 0; i < 22 * 9; ++i) {
    const float x = -494 + 45.0f * (i / 9), top = -173 - 12.0f * (i % 9);
    grid.items.push_back(MenuItem{true, x, top - 12, x + 45, top});
  }
  return grid;
}
}  // namespace

TEST_CASE("A grid: the cell under the point, edge to edge", "[menu_hit]") {
  const MenuList grid = Swatches();
  CHECK(ItemAt(grid, {-480, -175}) == 0);
  CHECK(ItemAt(grid, {-480, -186}) == 1);    // the next cell down
  CHECK(ItemAt(grid, {-449, -175}) == 9);    // the next column starts at its edge
  CHECK(ItemAt(grid, {493, -278}) == 197);   // the last cell
  CHECK(ItemAt(grid, {-480, -160}) == -1);   // above the grid
  CHECK(ItemAt(grid, {-500, -200}) == -1);   // left of it
}

TEST_CASE("A dialog's box runs left from its rightmost prompt", "[menu_hit]") {
  std::vector<Prompt> prompts{{300, -100, 20, 20, 0x1000}, {200, -100, 20, 50, 0x2000}, {0, -320, 20, 80, 0x20}};
  float left = 0, right = 0;
  DialogSpan(prompts, left, right);
  CHECK(right == 330);
  CHECK(left == 330 - kDialogWidth);
  DialogSpan({{0, -320, 20, 80, 0x20}}, left, right);  // bottom row only: no box
  CHECK(left == 0);
  CHECK(right == 0);
}

TEST_CASE("Two screens on one layer: the one opened last covers the lists it overlaps", "[menu_hit]") {
  using namespace fm4_static_recomp::input;
  Candidate party{true, 0x6A, 100, false, -584, -238, 98};
  party.max_y = 320, party.screen = 1, party.opened = 10;
  Candidate party_panel{true, 0x6A, 100, false, -315, 389, 160};
  party_panel.max_y = 304, party_panel.screen = 1, party_panel.opened = 11;
  Candidate hoppers{true, 0x6A, 100, false, -584, -238, 98};
  hoppers.max_y = 280, hoppers.screen = 2, hoppers.opened = 20;
  const Active a = ChooseActive({party, party_panel, hoppers});
  CHECK_FALSE(a.active[0]);
  CHECK_FALSE(a.active[1]);  // the whole covered screen
  CHECK(a.active[2]);
  // The covered screen's bottom bar goes with it (its prompts would be invisible buttons); the new one's stays.
  Candidate party_bar{false, 0x6A, 100};
  party_bar.screen = 1;
  Candidate hoppers_bar{false, 0x6A, 100};
  hoppers_bar.screen = 2;
  const Active bars = ChooseActive({party, party_bar, hoppers, hoppers_bar});
  CHECK_FALSE(bars.active[1]);
  CHECK(bars.active[3]);
  // The layer is no stacking order: the older screen on the higher layer is still the covered one.
  Candidate party_high = party;
  party_high.layer = 0x6B;
  const Active c = ChooseActive({party_high, hoppers});
  CHECK_FALSE(c.active[0]);
  CHECK(c.active[1]);
  // Side by side, they do not cover each other.
  Candidate right = hoppers;
  right.min_x = 100, right.max_x = 300;
  const Active b = ChooseActive({party, right});
  CHECK(b.active[0]);
  CHECK(b.active[1]);
}

TEST_CASE("Of two lists under a point, the one whose rows are there takes it", "[menu_hit]") {
  const auto column = [](float left, float right) {
    fm4_static_recomp::input::MenuList list;
    for (int row = 0; row < 3; ++row) {
      fm4_static_recomp::input::MenuItem item;
      item.shown = true;
      item.min_x = left;
      item.max_x = right;
      item.min_y = 200.0f - row * 40.0f;
      item.max_y = 240.0f - row * 40.0f;
      list.items.push_back(item);
    }
    return list;
  };
  // The player panel, first in the list of controls, reaches left over the menu column for its labels.
  const auto panel = column(-315, 389);
  const auto menu = column(-584, -320);
  const fm4_static_recomp::input::UiPoint on_menu{-400, 180};
  REQUIRE(fm4_static_recomp::input::ItemAt(panel, on_menu) >= 0);
  CHECK(fm4_static_recomp::input::ListUnder({&panel, &menu}, on_menu) == 1);
  CHECK(fm4_static_recomp::input::ListUnder({&menu, &panel}, on_menu) == 0);
  CHECK(fm4_static_recomp::input::ListUnder({&panel, &menu}, {100, 180}) == 0);
  CHECK(fm4_static_recomp::input::ListUnder({&panel, nullptr}, on_menu) == 0);  // only a label's reach: still that list
  CHECK(fm4_static_recomp::input::ListUnder({&panel, &menu}, {100, -500}) == -1);
}
