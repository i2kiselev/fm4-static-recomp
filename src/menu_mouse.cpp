// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "menu_mouse.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_set>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/func.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "game_hooks.h"
#include "guest_addresses.h"
#include "input/autovista_mouse.h"
#include "input/input_context.h"
#include "input/wheel_taps.h"

REXCVAR_DECLARE(bool, fm4_menu_mouse);
REXCVAR_DEFINE_BOOL(fm4_menu_mouse_log, false, "Forza Motorsport 4",
                    "Log what the mouse in menus sees on each pointer move: the UI containers, the lists and "
                    "prompt bars on screen (layer, frame), the active ones and the item under the pointer");

namespace fm4_static_recomp::menu_mouse {
namespace {

// CAnimatedScrollingListCtrl (the main menu's tiles, the side lists): its primary vtable, its items
// (a vector of item pointers) and the focused index.
using fm4_guest::kListVtable;
constexpr uint32_t kItemsBegin = 0x130, kItemsEnd = 0x134, kFocus = 0x158;
// A list's data source (+0x70) holds its entries (+0x18 begin, +0x1C end): an item's entry is where its
// data item sits there.
constexpr uint32_t kDataSource = 0x70, kEntriesBegin = 0x18, kEntriesEnd = 0x1C;
// Item -> its CUI4CustomObject -> UI4 node -> render element; an item is shown when it has data.
constexpr uint32_t kItemObject = 0x54, kObjectNode = 0x10, kNodeElement = 0x04, kItemData = 0x6C;
// Items of classes that carry no data item (+0x6C stays 0) are shown when drawn: TUNE SETUP's rows.
// Button items without data are a list's unused rows (drawn, but empty).
using fm4_guest::kTuneSliderVtable;
// CSlider rows (photo mode's effects: FOCUS, EXPOSURE...): CTuneSlider's base, with the same fields below.
using fm4_guest::kSliderVtable;
// CSpinner rows (AUDIO's SFX FOCUS, IN RACE MUSIC...): no data item either; a click steps to the next
// value (as RIGHT does), A does nothing on them.
using fm4_guest::kSpinnerVtable;
// A CTuneSlider's value as drawn on its bar, 0..1 (animated toward the value), and its value, range and
// step (doubles: TIRES' FRONT 30.0 psi, steps of 1.5). 0x82649340(slider, f1 = steps) moves it that many
// steps at once, the game's own way: rounding, clamping, the change sound, the screen told.
constexpr uint32_t kSliderFraction = 0xE0;
constexpr uint32_t kSliderValue = 0xA8, kSliderMin = 0xB8, kSliderMax = 0xC0, kSliderStepSize = 0xF8;
using fm4_guest::kSliderSteps;
// Render element: world translation, bounds (min, max; inverted when none), first child, next sibling.
constexpr uint32_t kWorldX = 0xAC, kWorldY = 0xB0, kMin = 0xBC, kMax = 0xC8, kChild = 0xDC, kSibling = 0xD8;
constexpr uint32_t kParent = 0xD4;
constexpr size_t kMaxItems = 256;
// A control's scene (its CUI4CustomObject +0x08): the frame it was last drawn (low 24 bits at +0x358) and its
// layer (+0x368: a dialog is one above the screen under it). Only the top layer drawn now takes the mouse.
constexpr uint32_t kObjectScene = 0x08, kSceneFrame = 0x358, kSceneLayer = 0x368;
// CHelpButtonBar (the prompts at the bottom): its prompts (0x34-byte entries, +0 = the CHelpButton);
// a CHelpButton's icon size and label width, and its data item whose +0x10 is the icon's texture path.
using fm4_guest::kBarVtable;
constexpr uint32_t kPromptsBegin = 0x12C, kPromptsEnd = 0x130, kPromptStride = 0x34;
constexpr uint32_t kPromptIcon = 0xF8, kPromptLabel = 0xFC, kPromptData = 0x6C, kDataIconPath = 0x10;
// CGridControl (PAINT CAR's colour swatches), as a container: the focused cell (cells go column by column),
// the columns and the cells to a column, a cell's pitch (floats); its CUI4CustomObject is at +0x54 as a
// list's is. Its render element holds a node whose children are the cells in that order, each at its
// place (bounds a margin around it).
using fm4_guest::kGridVtable;
constexpr uint32_t kGridFocus = 0x130, kGridFirst = 0x138, kGridColumns = 0x13C, kGridRows = 0x140;
constexpr uint32_t kGridShownColumns = 0x144, kGridPitchX = 0x14C, kGridPitchY = 0x150;
// The focus goes straight to the item under the pointer, the way the game's own code moves it (no D-pad
// steps through the items between):
// - a grid: 0x82641858(grid, cell) moves its focus to a cell (the move sound, scrolling it into view);
// - a list: 0x826209F0(list, entry, 0) takes the focus to an entry (the old item let go, the new one lit,
//   the list scrolled only when the entry is out of view, the owner told), after the move event its D-pad
//   move (0x82621030) sends the list (vtable +0x40: 0x82E023B4 / 0x82E025B4 forward / back).
// A carousel keeps its focus in one slot (+0x168, -1 for other lists) and scrolls to bring an entry
// there; a list shows +0x150 entries from +0x154. Hover only lights what is in view and never scrolls:
// a carousel's or a half-shown item takes the focus on a click.
using fm4_guest::kGridGoTo;
using fm4_guest::kListSetFocus;
constexpr uint32_t kListMoveEvent = 0x40;
using fm4_guest::kMoveForward;
using fm4_guest::kMoveBack;
constexpr uint32_t kListShownCount = 0x150, kListFirstShown = 0x154, kListFocusSlot = 0x168;
constexpr uint32_t kListItemBefore = 0x149;
constexpr int kMaxGridCells = 1024;

struct Shared {
  std::mutex mutex;
  std::optional<input::UiPoint> pointer;
  bool moved = false;
  bool click = false;
  bool held = false;  // the left button is down
  int right_clicks = 0;
  int notches = 0, sideways_notches = 0;
};

// Lists and grids whose focused item the mouse turned off (the pointer off their items; game thread only).
// The focus stays where it is (the game will not take "none" on most lists); only its item's highlight is
// off, until the pointer comes back onto an item or the next key or pad press (GateFocusBack).
// Items light and unlight through their own virtuals, as 0x826209F0 / 0x82640878 call them: +0x70(item, 0)
// lets go (animated), +0x6C(item, 0) lights (+0x68 for a list with +0x176 set; a grid's cells +0x68).
struct Unlit {
  uint32_t container = 0;
  uint32_t object = 0;  // the focused item
};
constexpr uint32_t kItemLetGo = 0x70, kItemLight = 0x6C, kItemLightAlt = 0x68, kListLightAlt = 0x176;
constexpr uint32_t kGridCellsBegin = 0x2A0, kGridCellsEnd = 0x2A4;
std::vector<Unlit> g_unlit;
std::atomic<bool> g_any_unlit{false};
std::atomic<bool> g_dialog_ok_only{false};
std::atomic<bool> g_modal{false};
bool g_wheel_along_row = false;  // the screen's lists are sideways strips: the wheel moves along them (Frame)

// The career hub (CMainMenu, 0x8257BBE0 makes it) hides its menu ~9 s after the last button: a timer at
// +0x180 (0x822D9220 sets it up) keeps the time of the last input (+0x00, seconds) and reads the time
// through its own clock (+0x50, called with the timer). The mouse is input too: each move, click or notch
// sets that time to now, so the menu stays while the mouse is in use, and comes back on a move once hidden.
// The instance is checked by its vtables before each use (+0x00 0x8203E384, +0x160 0x8203E478).
using fm4_guest::kMainMenuVtable;
using fm4_guest::kMainMenuInputVtable;
constexpr uint32_t kMainMenuInputAt = 0x160;
constexpr uint32_t kMainMenuIdleTimer = 0x180, kTimerSince = 0x00, kTimerClock = 0x50;
constexpr uint32_t kMainMenuBar = 0x178;  // its prompt bar: same scene as the hub's list
using Clock = std::chrono::steady_clock;
std::atomic<uint32_t> g_main_menu{0};

// The livery editor's layer placement (CLiveryLayerPosition, 0x824BD008 makes it): its tab list (+0x164, a
// list: MOVE, SIZE, SPIN, SKEW, TRANSPARENCY), the tabs' data (+0x19C: vtable +8 (index, 0) -> a tab, its mode
// at +4) and the editor (+0x1A0, state +0x64: 2, 3 or 4 while a layer is placed). The screen moves the layer
// with 0x823FCE08(editor, mode, f1, f2, r7 = 0): the stick's x and y times the mode's speed; mode 0 (MOVE)
// turns them from the camera's view into the layer's space first. The mouse goes the same way: the layer, the
// readouts and the redraw are the game's own.
using fm4_guest::kLiveryPlacementVtable;
using fm4_guest::kLiveryPlacementVtable2;
constexpr uint32_t kLiveryTabs = 0x164, kLiveryTabData = 0x19C, kLiveryEditor = 0x1A0, kLiveryEditorState = 0x64;
using fm4_guest::kLiveryAdjust;
using fm4_guest::kLiveryEditsGroup;
using fm4_guest::kLiveryAdjustGroup;  // the stick's branch
// After a change the screen shows it: 0x82515030(screen) (its readouts), then 0x825221D0(screen).
using fm4_guest::kLiveryShowChange;
using fm4_guest::kLiveryChanged;
std::atomic<uint32_t> g_livery{0};
std::atomic<uint32_t> g_livery_color{0};  // the last CLiveryColorSelect made (its vtable says it is still there)
using fm4_guest::kLiveryColorSelectVtable;
std::atomic<uint32_t> g_livery_layers{0};  // the last CLiveryLayerSelect made
using fm4_guest::kLiveryLayerSelectVtable;

// The main menu (CSplash, 0x825DF8F8 makes it): its bar (+0x168), its state (+0x239: 1 "Press A to start the
// game", 0 the menus), and its own data source (+0x164; FREE PLAY's menu, on the same screen, has another).
using fm4_guest::kSplashVtable;
constexpr uint32_t kSplashBar = 0x168;
constexpr uint32_t kSplashMainSource = 0x164;
constexpr uint32_t kButtonABit = 0x1000, kButtonBBit = 0x2000, kButtonBackBit = 0x20;
std::atomic<uint32_t> g_splash{0};
// The guest memory: whether the main menu is up is read from the game itself (MainMenuState), from any thread.
std::atomic<uint8_t*> g_base{nullptr};
constexpr uint32_t kSplashState = 0x239;  // byte: 1 "Press A to start the game", 0 the menus
double NowSeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct LiveryDrag {
  bool active = false;
  input::UiPoint last;
} g_livery_drag;
uint32_t g_last_list = 0;  // the last list seen on screen (game thread)
// A click that woke the hidden hub: played on the item under the pointer once its list is back.
Clock::time_point g_wake_click{};
Clock::time_point g_woke_at{};  // when a move or click last woke it
Clock::time_point g_last_keep{};  // a message box is up: key and pad presses are its, never held back
// The wheel moves the focus away from the pointer: until the mouse moves or clicks again, the resting
// pointer does not take it back (game thread only).
bool g_wheel_led = false;
std::atomic<bool> g_relight{false};

bool IsUnlit(uint32_t container) {
  return std::any_of(g_unlit.begin(), g_unlit.end(), [&](const Unlit& u) { return u.container == container; });
}

void Forget(uint32_t container) {
  std::erase_if(g_unlit, [&](const Unlit& u) { return u.container == container; });
  g_any_unlit = !g_unlit.empty();
}

// A slider being dragged (game thread only): which row, and the step size seen so far.
struct Drag {
  bool active = false;
  uint32_t container = 0;
  int item = -1;
} g_drag;

double Double(const uint8_t* base, uint32_t a);

// Calls one of the game's functions from the UI frame (game thread), on the stack below the caller's;
// `setup` puts its arguments in. The caller's FPU mode is kept.
template <typename Setup>
bool CallGuest(PPCContext& ctx, uint8_t* base, uint32_t stack, uint32_t address, Setup setup,
               double* f1 = nullptr, uint32_t* r3 = nullptr) {
  PPCFunc* const function = rex::runtime::ResolveIndirectFunction(address);
  if (!function) return false;
  const uint32_t saved_csr = ctx.fpscr.csr;
  PPCContext nested = ctx;
  nested.dispatch_address = 0;
  nested.lr = 0;
  nested.r1.u32 = stack - 0x100;
  setup(nested);
  function(nested, base);
  if (f1) *f1 = nested.f1.f64;
  if (r3) *r3 = nested.r3.u32;
  ctx.fpscr.csr = saved_csr;
  ctx.fpscr.setcsr(saved_csr);
  return true;
}

// Moves a slider by whole steps toward the place on its bar the pointer asks for, in one call of its
// own step routine.
void DragSlider(PPCContext& ctx, uint8_t* base, uint32_t stack, const input::MenuItem& item, float target) {
  const double value = Double(base, item.id + kSliderValue), low = Double(base, item.id + kSliderMin);
  const double high = Double(base, item.id + kSliderMax), step = Double(base, item.id + kSliderStepSize);
  if (!(step > 0) || !(high > low) || !std::isfinite(value)) return;
  const double steps = std::round((low + double(target) * (high - low) - value) / step);
  if (steps == 0 || std::fabs(steps) > 10000) return;
  CallGuest(ctx, base, stack, kSliderSteps, [&](PPCContext& c) {
    c.r3.u64 = item.id;
    c.f1.f64 = steps;
  });
  if (REXCVAR_GET(fm4_menu_mouse_log)) {
    REXLOG_INFO("Menu mouse: slider {:08X} {} -> {} steps of {} toward {:.3f}", item.id, value, steps, step, target);
  }
}

void StopDrag() { g_drag = Drag{}; }

Shared& State() {
  static Shared state;
  return state;
}

// The Autovista viewer: open while its camera updates (each main loop tick); its mouse, under its lock.
std::atomic<int64_t> g_autovista_tick_ms{0};
std::mutex g_autovista_mutex;
input::AutovistaMouse g_autovista;
// The paint screens' car (the colour choice, the livery editor): a right drag over the car turns the camera
// round it with the right stick, as fast as the pointer moves; a right click that does not move is still back.
// Under g_autovista_mutex.
input::AutovistaMouse g_car_view;
constexpr int64_t kAutovistaGoneMs = 300;
constexpr uint32_t kButtonYBit = 0x8000;
std::atomic<uint32_t> g_autovista_camera{0};
// A list is up over the viewer's camera: the mouse is the list's, and the sticks (which work the list) are left alone.
std::atomic<bool> g_viewer_list_up{false};

int64_t SteadyMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// The UI photo mode (TAKE PHOTO): its camera (CUI4PhotoModeCamera) exists only while it is open. The hook
// Fm4PhotoModeCameraUpdate gives it; each UI frame checks it still carries its own vtable (its destructor puts
// its bases' back).
using fm4_guest::kPhotoCameraVtable;
std::atomic<uint32_t> g_photo_camera{0};
std::atomic<bool> g_photo_live{false};

// The race's photo mode (CPhotoModeIngame) has a camera of its own, on the same sticks as the UI photo mode's.
using fm4_guest::kRacePhotoVtable;
std::atomic<uint32_t> g_race_submenu{0};
std::atomic<bool> g_race_photo{false};
std::atomic<bool> g_title_up{false};
std::atomic<bool> g_sidebar_back_icon{false};
// Right clicks go back one at a time, a screen's transition apart; one made meanwhile waits its turn, more are dropped.
constexpr int64_t kBackSpacingMs = 700;
constexpr int64_t kBackWaitMs = 900;  // a click still unanswered after this is forgotten
int64_t g_back_ready_ms = 0, g_back_expires_ms = 0;
bool g_back_waiting = false;

bool PhotoModeUp() { return g_photo_live.load(std::memory_order_relaxed) || g_race_photo.load(std::memory_order_relaxed); }

// The Autovista viewer or its photo mode: the mouse works the camera's sticks there.
bool AutovistaUp() {
  return PhotoModeUp() || SteadyMs() - g_autovista_tick_ms.load(std::memory_order_relaxed) < kAutovistaGoneMs;
}

// A press in the viewer's picture, not on its prompts (the bottom row).
bool InAutovistaPicture(const std::optional<input::UiPoint>& point) {
  return point && point->y > input::kBottomRowY && AutovistaUp() && !g_viewer_list_up;
}

int16_t ToStick(double v) { return int16_t(std::lround(std::clamp(v, -1.0, 1.0) * 32767)); }

bool CarViewUp();
bool OverCarView(const std::optional<input::UiPoint>& point) {
  return point && point->y > input::kBottomRowY && !AutovistaUp() && CarViewUp();
}

std::mutex g_containers_mutex;
std::unordered_set<uint32_t> g_containers;
// The screens' focus groups (CUI4CtrlFocusGroup): its controls [+0x18, +0x1C), the one the D-pad works +0x28.
// kGroupSetFocus(group, control, true) is what the D-pad does on moving to another control (it takes the
// others' focus away and tells the screen, which then follows that control: the livery colour screen's
// preview and the colour OK picks).
std::unordered_set<uint32_t> g_groups;  // under g_containers_mutex
// Screens that hide their menu when idle. Under the same mutex. The car colour choice (CCarColorSelect): +0x1F8
// the idle start (game ticks), +0x201 hidden, kScreenWake shows the menu and restarts the idle. The pre-race
// menu (CPreRace): a timer at +0x168 (8 s), +0x1C0 hidden, kPreRaceShow shows it, kTimerRestart restarts it.
// As a press does on each.
std::unordered_set<uint32_t> g_idle_screens;
using fm4_guest::kPreRaceVtable;
using fm4_guest::kCarColorVtable;
using fm4_guest::kScreenWake;
using fm4_guest::kGameTicks;
constexpr uint32_t kIdleSince = 0x1F8, kIdleHidden = 0x201;
using fm4_guest::kPreRaceShow;
constexpr uint32_t kPreRaceTimer = 0x168, kPreRaceHidden = 0x1C0;
using fm4_guest::kTimerRestart;
using fm4_guest::kGroupVtable;
constexpr uint32_t kGroupControlsBegin = 0x18, kGroupControlsEnd = 0x1C;
using fm4_guest::kGroupSetFocus;
constexpr uint32_t kGroupFocused = 0x28;

bool GuestPointer(uint32_t p) { return p >= 0x10000 && p < 0x9F000000 && (p & 3) == 0; }


// Guest memory is read only where its host page is mapped: a list or a bar freed and reused under us
// must not lead a read into a hole (the containers are the game's; we only look). What a page was this frame is
// remembered, as asking the OS costs far more than the read: a direct-mapped table, each entry one word (frame,
// page, readable) as the keyboard filter's thread reads through it too.
struct PageCache {
  static constexpr uint32_t kPage = 4096, kSlots = 4096;
  std::array<std::atomic<uint64_t>, kSlots> entries{};
  std::atomic<uint32_t> frame{1};
} g_pages;

bool PageReadable(const uint8_t* base, uint32_t page) {
  const uint64_t tag = uint64_t(g_pages.frame.load(std::memory_order_relaxed)) << 32 | uint64_t(page) << 1;
  std::atomic<uint64_t>& entry = g_pages.entries[(page * 2654435761u) >> 20];
  const uint64_t seen = entry.load(std::memory_order_relaxed);
  if ((seen & ~uint64_t(1)) == tag) return (seen & 1) != 0;
  // Not in the table: the regions already asked about this frame answer most of these, one OS call each.
  struct Region {
    const uint8_t* begin = nullptr;
    const uint8_t* end = nullptr;
    bool ok = false;
  };
  thread_local std::array<Region, 32> regions;
  thread_local uint32_t regions_frame = 0;
  thread_local size_t regions_used = 0, regions_next = 0;
  const uint32_t frame = g_pages.frame.load(std::memory_order_relaxed);
  if (regions_frame != frame) regions_frame = frame, regions_used = regions_next = 0;
  const uint8_t* const at = base + size_t(page) * PageCache::kPage;
  for (size_t i = 0; i < regions_used; ++i) {
    if (at >= regions[i].begin && at < regions[i].end) {
      entry.store(tag | (regions[i].ok ? 1 : 0), std::memory_order_relaxed);
      return regions[i].ok;
    }
  }
  size_t length = PageCache::kPage;
  rex::memory::PageAccess access = rex::memory::PageAccess::kNoAccess;
  const bool ok = rex::memory::QueryProtect(const_cast<uint8_t*>(at), length, access) &&
                  access != rex::memory::PageAccess::kNoAccess;
  regions[regions_next] = {at, at + std::max<size_t>(length, PageCache::kPage), ok};
  regions_next = (regions_next + 1) % regions.size();
  regions_used = std::min(regions_used + 1, regions.size());
  entry.store(tag | (ok ? 1 : 0), std::memory_order_relaxed);
  return ok;
}

bool Readable(const uint8_t* base, uint32_t a, uint32_t size) {
  for (uint32_t page = a / PageCache::kPage; page <= (a + size - 1) / PageCache::kPage; ++page) {
    if (!PageReadable(base, page)) return false;
  }
  return true;
}

// 0 where the memory cannot be read (no pointer leads on from it).
uint32_t Word(const uint8_t* base, uint32_t a) {
  if (a < 0x10000 || !Readable(base, a, 4)) return 0;
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}

// NaN where the memory cannot be read.
double Double(const uint8_t* base, uint32_t a) {
  if (a < 0x10000 || !Readable(base, a, 8)) return std::numeric_limits<double>::quiet_NaN();
  uint64_t v;
  std::memcpy(&v, base + a, 8);
  v = __builtin_bswap64(v);
  double d;
  std::memcpy(&d, &v, 8);
  return d;
}

// NaN where the memory cannot be read (every bounds test fails on it).
float Float(const uint8_t* base, uint32_t a) {
  if (a < 0x10000 || !Readable(base, a, 4)) return std::numeric_limits<float>::quiet_NaN();
  const uint32_t v = Word(base, a);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

uint8_t Byte(const uint8_t* base, uint32_t a) { return a >= 0x10000 && Readable(base, a, 1) ? base[a] : 0; }

// A render element is drawn when its visible byte (+0x08) is set; hidden ones (another tab's page,
// a faded-out panel) are skipped with all they hold.
constexpr uint32_t kVisible = 0x08;
bool Visible(const uint8_t* base, uint32_t element) { return Byte(base, element + kVisible) != 0; }

// An element's opacity as drawn (+0xE4: its own times its parents'). A tab page swapped out keeps its
// visible bytes and fades to 0 here (PAINT CAR's NORMAL / METALLIC / SPECIAL COLORS).
constexpr uint32_t kDrawnOpacity = 0xE4;
// An element's own opacity (+0xE0): 0 hides it with all it holds, though its visible byte stays set. A row whose
// every part is hidden so is not there at all (AUDIO's unused last row).
constexpr uint32_t kOwnAlpha = 0xE0;
bool Hidden(const uint8_t* base, uint32_t element) { return !(Float(base, element + kOwnAlpha) > 0.01f); }
bool AllPartsHidden(const uint8_t* base, uint32_t element) {
  uint32_t child = Word(base, element + 0xDC);
  if (!GuestPointer(child)) return false;
  for (int n = 0; GuestPointer(child) && n < 64; ++n, child = Word(base, child + 0xD8)) {
    if (!Hidden(base, child)) return false;
  }
  return true;
}

// It and every element above it are visible, and it is not faded out.
bool ShownOnScreen(const uint8_t* base, uint32_t element) {
  if (GuestPointer(element) && !(Float(base, element + kDrawnOpacity) > 0)) return false;
  for (int n = 0; GuestPointer(element) && n < 32; ++n) {
    if (!Visible(base, element)) return false;
    element = Word(base, element + kParent);
  }
  return true;
}

void Extend(input::MenuItem& out, bool& any, float min_x, float min_y, float max_x, float max_y) {
  if (!any) {
    out.min_x = min_x, out.min_y = min_y, out.max_x = max_x, out.max_y = max_y;
    any = true;
  } else {
    out.min_x = std::min(out.min_x, min_x), out.min_y = std::min(out.min_y, min_y);
    out.max_x = std::max(out.max_x, max_x), out.max_y = std::max(out.max_y, max_y);
  }
}

// The union of the visible element's and its visible descendants' drawn bounds, in UI space. A leaf
// with no bounds (a text) counts as the point it is drawn at.
void Bounds(const uint8_t* base, uint32_t element, int depth, input::MenuItem& out, bool& any) {
  if (!GuestPointer(element) || depth > 10 || !Visible(base, element)) return;
  const float x = Float(base, element + kWorldX), y = Float(base, element + kWorldY);
  const float min_x = Float(base, element + kMin), min_y = Float(base, element + kMin + 4);
  const float max_x = Float(base, element + kMax), max_y = Float(base, element + kMax + 4);
  const uint32_t first_child = Word(base, element + kChild);
  if (min_x <= max_x && min_y <= max_y && max_x - min_x < 4096 && max_y - min_y < 4096) {
    Extend(out, any, x + min_x, y + min_y, x + max_x, y + max_y);
  } else if (!GuestPointer(first_child) && std::isfinite(x) && std::isfinite(y) && std::fabs(x) < 2000 &&
             std::fabs(y) < 2000) {
    Extend(out, any, x, y, x, y);
  }
  uint32_t child = first_child;
  for (int n = 0; GuestPointer(child) && n < 64; ++n) {
    Bounds(base, child, depth + 1, out, any);
    child = Word(base, child + kSibling);
  }
}

// A carousel tile's panel: the largest image in its subtree, as drawn (its box scaled by +0x7C across, +0x90 down).
constexpr uint8_t kElementImage = 2;
void LargestImage(const uint8_t* base, uint32_t element, int depth, input::MenuItem& out, float& area) {
  if (!GuestPointer(element) || depth > 10 || !Visible(base, element)) return;
  if (Byte(base, element + 0x0A) == kElementImage) {
    const float min_x = Float(base, element + kMin), max_x = Float(base, element + kMax);
    const float min_y = Float(base, element + kMin + 4), max_y = Float(base, element + kMax + 4);
    const float sx = std::fabs(Float(base, element + 0x7C)), sy = std::fabs(Float(base, element + 0x90));
    const float width = (max_x - min_x) * sx, height = (max_y - min_y) * sy;
    if (min_x <= max_x && min_y <= max_y && width < 1280 && height < 720 && width * height > area) {
      const float x = Float(base, element + kWorldX), y = Float(base, element + kWorldY);
      out.min_x = x + min_x * sx, out.max_x = x + max_x * sx;
      out.min_y = y + min_y * sy, out.max_y = y + max_y * sy;
      area = width * height;
    }
  }
  uint32_t child = Word(base, element + kChild);
  for (int n = 0; GuestPointer(child) && n < 64; ++n) {
    LargestImage(base, child, depth + 1, out, area);
    child = Word(base, child + kSibling);
  }
}

// The widest flat box in a row's subtree: its slider's bar.
void WidestBar(const uint8_t* base, uint32_t element, int depth, float& left, float& right) {
  if (!GuestPointer(element) || depth > 10 || !Visible(base, element)) return;
  const float min_x = Float(base, element + kMin), min_y = Float(base, element + kMin + 4);
  const float max_x = Float(base, element + kMax), max_y = Float(base, element + kMax + 4);
  if (min_x <= max_x && max_y - min_y <= 24 && max_x - min_x >= 100 && max_x - min_x > right - left) {
    const float x = Float(base, element + kWorldX);
    left = x + min_x, right = x + max_x;
  }
  uint32_t child = Word(base, element + kChild);
  for (int n = 0; GuestPointer(child) && n < 64; ++n) {
    WidestBar(base, child, depth + 1, left, right);
    child = Word(base, child + kSibling);
  }
}

// A CSlider's bar is a 100-unit image scaled to size (+0x7C across, +0x90 down): the widest flat one.
void WidestScaledBar(const uint8_t* base, uint32_t element, int depth, float& left, float& right) {
  if (!GuestPointer(element) || depth > 10 || !Visible(base, element)) return;
  const float min_x = Float(base, element + kMin), max_x = Float(base, element + kMax);
  const float min_y = Float(base, element + kMin + 4), max_y = Float(base, element + kMax + 4);
  const float scale_x = std::fabs(Float(base, element + 0x7C)), scale_y = std::fabs(Float(base, element + 0x90));
  const float width = (max_x - min_x) * scale_x, height = (max_y - min_y) * scale_y;
  if (min_x <= max_x && height <= 24 && width >= 100 && width > right - left) {
    const float x = Float(base, element + kWorldX);
    left = x + min_x * scale_x, right = x + max_x * scale_x;
  }
  uint32_t child = Word(base, element + kChild);
  for (int n = 0; GuestPointer(child) && n < 64; ++n) {
    WidestScaledBar(base, child, depth + 1, left, right);
    child = Word(base, child + kSibling);
  }
}

void SliderOf(const uint8_t* base, uint32_t entry, uint32_t element, input::MenuItem& item) {
  float left = 0, right = 0;
  WidestBar(base, element, 0, left, right);
  if (right <= left) WidestScaledBar(base, element, 0, left, right);
  const float fraction = Float(base, entry + kSliderFraction);
  if (right > left && fraction >= 0 && fraction <= 1) {
    item.slider = true;
    item.bar_left = left, item.bar_right = right, item.fraction = fraction;
    item.id = entry;
  }
}

uint32_t ElementOf(const uint8_t* base, uint32_t object) {
  const uint32_t node = GuestPointer(object) ? Word(base, object + kObjectNode) : 0;
  return GuestPointer(node) ? Word(base, node + kNodeElement) : 0;
}

// The Autovista viewer's parts (CCarPartSelect: the wheels' and the engine's info, the doors...) register
// with the Kinect cursor (its singleton at 0x82E10C1C; a tree set of objects at +4: head node +8, size
// +0xC; nodes left +0, parent +4, right +8, the object +0xC, "is the head" +0x11). Each shown part
// (+0x68's top byte 1) draws a marker: a dot, or, for the one where the walk stands, the full icon that
// A opens. The marker's root element (the object's +0x54 node) sits at the icon's foot, its scale
// (+0x7C) shrinking with distance; the icon's box spans 50 units each side and 105 up at scale 1. The
// marker's clip grows its ring (the root's first child's first child's first child's fourth child) to
// 2.3 times the root's scale for the full icon; a dot's ring stays at the root's scale.
using fm4_guest::kNatalCursorPointer;
constexpr uint32_t kCursorSetHead = 0x08, kCursorSetSize = 0x0C;
constexpr uint32_t kSetNodeObject = 0x0C, kSetNodeIsHead = 0x11;
using fm4_guest::kCarPartVtable;
// The doors, bonnet, boot and spoiler (CCarPartAnimate) draw another marker: its root at the icon's centre, the full
// icon a circle (the root's 1st child's 1st child's 1st child's 1st child's 1st child's 1st child) drawn only on the
// part where the walk stands, about 55 units round at scale 1; elsewhere a dot.
using fm4_guest::kCarPartAnimateVtable;
constexpr float kAnimateIconRadius = 62;  // at scale 1, a little margin
constexpr uint32_t kCarPartShown = 0x68;
constexpr uint32_t kElementScale = 0x7C;
constexpr float kIconHalfWidth = 55, kIconBelow = 20, kIconAbove = 115;  // at scale 1, a little margin
constexpr float kIconRingFull = 1.6f;  // ring / root scale above which the marker is the full icon

uint32_t NthChild(const uint8_t* base, uint32_t element, int n) {
  uint32_t child = GuestPointer(element) ? Word(base, element + kChild) : 0;
  for (int i = 0; i < n && GuestPointer(child); ++i) child = Word(base, child + kSibling);
  return GuestPointer(child) ? child : 0;
}

// True when the point is on the focused part's icon.
bool OnAutovistaIcon(const uint8_t* base, input::UiPoint point) {
  const uint32_t cursor = Word(base, kNatalCursorPointer);
  const uint32_t head = GuestPointer(cursor) ? Word(base, cursor + kCursorSetHead) : 0;
  const uint32_t size = GuestPointer(cursor) ? Word(base, cursor + kCursorSetSize) : 0;
  if (!GuestPointer(head) || size == 0 || size > 256) return false;
  // In order from the leftmost node: each node's successor.
  uint32_t node = Word(base, head);
  for (uint32_t seen = 0; GuestPointer(node) && node != head && seen < size; ++seen) {
    if (Byte(base, node + kSetNodeIsHead) != 0) break;
    const uint32_t part = Word(base, node + kSetNodeObject);
    if (GuestPointer(part) && Word(base, part) == kCarPartAnimateVtable && Byte(base, part + kCarPartShown) == 1) {
      const uint32_t root = ElementOf(base, Word(base, part + kItemObject));
      uint32_t circle = root;
      for (int level = 0; level < 6 && circle; ++level) circle = NthChild(base, circle, 0);
      const float x = Float(base, root + kWorldX), y = Float(base, root + kWorldY);
      const float s = Float(base, root + kElementScale);
      const bool full = circle && Float(base, circle + kDrawnOpacity) > 0.5f;
      if (full && s > 0.05f && s < 4 && std::fabs(point.x - x) <= kAnimateIconRadius * s &&
          std::fabs(point.y - y) <= kAnimateIconRadius * s) {
        return true;
      }
    }
    if (GuestPointer(part) && Word(base, part) == kCarPartVtable && Byte(base, part + kCarPartShown) == 1) {
      const uint32_t root = ElementOf(base, Word(base, part + kItemObject));
      const float x = Float(base, root + kWorldX), y = Float(base, root + kWorldY);
      const float s = Float(base, root + kElementScale);
      const uint32_t ring = NthChild(base, NthChild(base, NthChild(base, NthChild(base, root, 0), 0), 0), 3);
      const bool full = ring && s > 0.05f && Float(base, ring + kElementScale) / s > kIconRingFull;
      if (full && s < 4 && point.x >= x - kIconHalfWidth * s &&
          point.x <= x + kIconHalfWidth * s && point.y >= y - kIconBelow * s && point.y <= y + kIconAbove * s) {
        return true;
      }
    }
    // The successor: the right subtree's leftmost node, else up until coming from a left child.
    const uint32_t right = Word(base, node + 8);
    if (GuestPointer(right) && Byte(base, right + kSetNodeIsHead) == 0) {
      node = right;
      for (int depth = 0; depth < 64; ++depth) {
        const uint32_t left = Word(base, node);
        if (!GuestPointer(left) || Byte(base, left + kSetNodeIsHead) != 0) break;
        node = left;
      }
    } else {
      uint32_t parent = Word(base, node + 4);
      for (int depth = 0; depth < 64 && GuestPointer(parent) && parent != head && node == Word(base, parent + 8);
           ++depth) {
        node = parent;
        parent = Word(base, node + 4);
      }
      node = parent;
    }
  }
  return false;
}

// HIDE UI (the pad's Y) in the viewer: the camera's +0x14C points into the home space screen (CNatalHomespace,
// 0x388 before it), which keeps a block 0x188 after it: +0x60 the viewer's prompt bar, +0x50 a byte that is 1 while
// the UI shows.
using fm4_guest::kWalkaroundCameraVtable;
constexpr uint32_t kCameraSpaceLink = 0x14C;
using fm4_guest::kHomespaceVtable;
constexpr uint32_t kLinkToHomespace = 0x388;
constexpr uint32_t kSpaceLinkToUi = 0x188, kUiBar = 0x60, kUiShown = 0x50;

bool ViewerUiHidden(const uint8_t* base) {
  const uint32_t camera = g_autovista_camera;
  if (!GuestPointer(camera) || Word(base, camera) != kWalkaroundCameraVtable) return false;
  const uint32_t link = Word(base, camera + kCameraSpaceLink);
  if (!GuestPointer(link) || Word(base, link - kLinkToHomespace) != kHomespaceVtable) return false;
  const uint32_t bar = Word(base, link + kSpaceLinkToUi + kUiBar);
  if (!GuestPointer(bar) || Word(base, bar) != kBarVtable) return false;
  return Byte(base, link + kSpaceLinkToUi + kUiShown) == 0;
}

// A grid as the game has it now: its cells, each the pitch around the middle of its drawn bounds.
std::optional<input::MenuList> ReadGrid(const uint8_t* base, uint32_t grid) {
  input::MenuList list;
  list.focus = int32_t(Word(base, grid + kGridFocus));
  const int columns = int32_t(Word(base, grid + kGridColumns)), rows = int32_t(Word(base, grid + kGridRows));
  int shown = int32_t(Word(base, grid + kGridShownColumns));
  if (shown <= 0 || shown > columns) shown = columns;
  const int first = int32_t(Word(base, grid + kGridFirst));  // the first cell on screen (a scrolled grid)
  const float pitch_x = Float(base, grid + kGridPitchX), pitch_y = Float(base, grid + kGridPitchY);
  if (columns <= 0 || rows <= 0 || columns * rows > kMaxGridCells || list.focus < 0 ||
      list.focus >= columns * rows || first < 0 || first >= columns * rows || !(pitch_x > 0 && pitch_x < 400) ||
      !(pitch_y > 0 && pitch_y < 400)) {
    return std::nullopt;
  }
  const uint32_t element = ElementOf(base, Word(base, grid + kItemObject));
  if (!ShownOnScreen(base, element)) return std::nullopt;
  // The node holding the cells on screen, whole columns of them: the shown columns, and one more in a grid
  // that scrolls (DECAL SELECT keeps 6 columns of cells for 5 shown).
  for (uint32_t holder = Word(base, element + kChild), n = 0; GuestPointer(holder) && n < 16; ++n) {
    std::vector<uint32_t> cells;
    for (uint32_t cell = Word(base, holder + kChild);
         GuestPointer(cell) && int(cells.size()) <= (shown + 1) * rows; cell = Word(base, cell + kSibling)) {
      cells.push_back(cell);
    }
    const int count = int(cells.size());
    if (count >= shown * rows && count <= (shown + 1) * rows && count % rows == 0 && Visible(base, holder)) {
      list.grid_rows = rows;
      for (uint32_t cell : cells) {
        input::MenuItem item;
        item.index = first + int(list.items.size());
        // The cell's object (the grid's own vector of them, in the same order).
        const uint32_t objects = Word(base, grid + kGridCellsBegin);
        const uint32_t objects_end = Word(base, grid + kGridCellsEnd);
        const uint32_t slot = objects + 4 * uint32_t(list.items.size());
        if (GuestPointer(objects) && slot < objects_end && objects_end - objects <= 4 * kMaxGridCells) {
          item.id = Word(base, slot);
        }
        // Past the last cell, or in the column kept for scrolling (off the grid's edge): not shown, so the
        // pointer never lights a cell that would scroll the grid under it.
        if (item.index >= columns * rows || item.index / rows - first / rows >= shown) {
          list.items.push_back(item);  // past the last cell: not shown
          continue;
        }
        bool any = false;
        Bounds(base, cell, 0, item, any);
        item.shown = any;
        if (any) {
          const float x = (item.min_x + item.max_x) / 2, y = (item.min_y + item.max_y) / 2;
          item.min_x = x - pitch_x / 2, item.max_x = x + pitch_x / 2;
          item.min_y = y - pitch_y / 2, item.max_y = y + pitch_y / 2;
        }
        list.items.push_back(item);
      }
      return list;
    }
    holder = Word(base, holder + kSibling);
  }
  return std::nullopt;
}

// A focus group whose controls are all sliders, with no list around them (COLOR FINE TUNE's HUE, SATURATION and
// BRIGHTNESS): the mouse works it as a column of slider rows, the group's focused one being the list's focus.
bool SliderGroup(const uint8_t* base, uint32_t group) {
  if (!GuestPointer(group) || Word(base, group) != kGroupVtable) return false;
  const uint32_t first = Word(base, group + kGroupControlsBegin), last = Word(base, group + kGroupControlsEnd);
  if (!GuestPointer(first) || last <= first || last - first > 8 * 4) return false;
  for (uint32_t at = first; at < last; at += 4) {
    const uint32_t control = Word(base, at);
    if (!GuestPointer(control) || Word(base, control) != kSliderVtable) return false;
  }
  return true;
}

std::optional<input::MenuList> ReadSliderGroup(const uint8_t* base, uint32_t group) {
  input::MenuList list;
  const uint32_t first = Word(base, group + kGroupControlsBegin), last = Word(base, group + kGroupControlsEnd);
  const uint32_t focused = Word(base, group + kGroupFocused);
  for (uint32_t at = first; at < last; at += 4) {
    const uint32_t slider = Word(base, at);
    if (slider == focused) list.focus = int(list.items.size());
    input::MenuItem item;
    item.id = slider;
    const uint32_t element = ElementOf(base, Word(base, slider + kItemObject));
    bool any = false;
    if (ShownOnScreen(base, element)) Bounds(base, element, 0, item, any);
    item.shown = any;
    if (any) {
      item.anchor_y = Float(base, element + kWorldY);
      SliderOf(base, slider, element, item);
    }
    list.items.push_back(item);
  }
  if (list.focus < 0) return std::nullopt;
  return list;
}

// The list as the game has it now, when it is a focused list (or a grid); nullopt otherwise.
std::optional<input::MenuList> ReadList(const uint8_t* base, uint32_t container) {
  if (GuestPointer(container) && Word(base, container) == kGridVtable) return ReadGrid(base, container);
  if (SliderGroup(base, container)) return ReadSliderGroup(base, container);
  if (!GuestPointer(container) || Word(base, container) != kListVtable) return std::nullopt;
  input::MenuList list;
  list.focus = int32_t(Word(base, container + kFocus));
  if (list.focus < 0) return std::nullopt;
  const uint32_t begin = Word(base, container + kItemsBegin), end = Word(base, container + kItemsEnd);
  if (!GuestPointer(begin) || end < begin || (end - begin) / 4 > kMaxItems) return std::nullopt;
  // The entries, to tell which one each (recycled) item shows.
  std::vector<uint32_t> entries;
  if (const uint32_t source = Word(base, container + kDataSource); GuestPointer(source)) {
    const uint32_t first = Word(base, source + kEntriesBegin), last = Word(base, source + kEntriesEnd);
    if (GuestPointer(first) && last >= first && (last - first) / 4 <= kMaxItems) {
      for (uint32_t e = first; e < last; e += 4) entries.push_back(Word(base, e));
    }
    // Only a plain array of entries (PAINT CAR's carousel); the main menu's source is (key, entry) pairs, and
    // there the item's place is already the entry.
    if (std::any_of(entries.begin(), entries.end(), [](uint32_t e) { return !GuestPointer(e); })) entries.clear();
  }
  // Otherwise, as 0x826209F0 lays a scrolled list out: item i shows entry i + the first shown (+0x154; below
  // 0 at a carousel's start), less one when the list keeps an item before the first (+0x149).
  const int first_shown = int32_t(Word(base, container + kListFirstShown));
  const int before = Byte(base, container + kListItemBefore) != 0 ? 1 : 0;
  const bool scrolled = first_shown != 0 || before != 0;
  for (uint32_t p = begin; p < end; p += 4) {
    input::MenuItem item;
    const uint32_t entry = Word(base, p);
    if (GuestPointer(entry) &&
        (Word(base, entry + kItemData) != 0 || Word(base, entry) == kTuneSliderVtable ||
         Word(base, entry) == kSliderVtable || Word(base, entry) == kSpinnerVtable)) {
      item.id = entry;
      const uint32_t object = Word(base, entry + kItemObject);
      const uint32_t node = GuestPointer(object) ? Word(base, object + kObjectNode) : 0;
      const uint32_t element = GuestPointer(node) ? Word(base, node + kNodeElement) : 0;
      bool any = false;
      if (ShownOnScreen(base, element)) Bounds(base, element, 0, item, any);
      item.shown = any && !AllPartsHidden(base, element);
      if (item.shown) item.anchor_y = Float(base, element + kWorldY);
      if (any && (Word(base, entry) == kTuneSliderVtable || Word(base, entry) == kSliderVtable)) {
        SliderOf(base, entry, element, item);
      }
      if (any && Word(base, entry) == kSpinnerVtable) item.spinner = true;
      if (const uint32_t data = Word(base, entry + kItemData); data != 0) {
        for (size_t k = 0; k < entries.size(); ++k) {
          if (entries[k] == data) item.index = int(k);
        }
      }
      if (item.index < 0 && scrolled) {
        item.index = int(list.items.size()) + first_shown - before;
        if (item.index < 0) item.shown = false;  // a slot before the first entry
      }
    }
    list.items.push_back(item);
  }
  if (!scrolled && list.focus >= int(std::max(list.items.size(), entries.size()))) return std::nullopt;
  // A carousel's tiles, of whatever class: each is its panel as drawn (the unscaled boxes of its parts run wide).
  if (input::HorizontalList(list)) {
    for (input::MenuItem& item : list.items) {
      if (!item.shown || !item.id) continue;
      const uint32_t element = ElementOf(base, Word(base, item.id + kItemObject));
      input::MenuItem panel;
      float area = 0;
      LargestImage(base, element, 0, panel, area);
      const float width = panel.max_x - panel.min_x, height = panel.max_y - panel.min_y;
      const float x = Float(base, element + kWorldX);
      if (width >= 120 && width <= 640 && height >= 60 && height <= 500 && x >= panel.min_x && x <= panel.max_x) {
        item.min_x = panel.min_x, item.max_x = panel.max_x;
        item.min_y = panel.min_y, item.max_y = panel.max_y;
      }
    }
  }
  return list;
}

bool CallVirtual(PPCContext& ctx, uint8_t* base, uint32_t stack, uint32_t object, uint32_t offset, uint32_t arg);

void StoreWord(uint8_t* base, uint32_t a, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(base + a, &value, 4);
}

// The prompts a bar shows now (those laid out somewhere: a hidden bar's sit at the origin).
std::vector<input::Prompt> ReadPrompts(const uint8_t* base, uint32_t container) {
  std::vector<input::Prompt> prompts;
  if (!GuestPointer(container) || Word(base, container) != kBarVtable) return prompts;
  const uint32_t begin = Word(base, container + kPromptsBegin), end = Word(base, container + kPromptsEnd);
  if (!GuestPointer(begin) || end < begin || (end - begin) / kPromptStride > 16) return prompts;
  for (uint32_t p = begin; p + kPromptStride <= end; p += kPromptStride) {
    const uint32_t button = Word(base, p);
    if (!GuestPointer(button)) continue;
    const uint32_t object = Word(base, button + kItemObject);
    const uint32_t node = GuestPointer(object) ? Word(base, object + kObjectNode) : 0;
    const uint32_t element = GuestPointer(node) ? Word(base, node + kNodeElement) : 0;
    const uint32_t data = Word(base, button + kPromptData);
    const uint32_t path = GuestPointer(data) ? Word(base, data + kDataIconPath) : 0;
    if (!GuestPointer(element) || !GuestPointer(path) || !ShownOnScreen(base, element)) continue;
    input::Prompt prompt;
    const uint32_t inner = NthChild(base, element, 0);
    prompt.x = Float(base, (inner ? inner : element) + kWorldX);
    prompt.y = Float(base, element + kWorldY);
    if (prompt.x == 0 && prompt.y == 0) continue;
    prompt.icon = Float(base, button + kPromptIcon);
    prompt.label = Float(base, button + kPromptLabel);
    // A prompt the bar has laid out has a measured label; a bar left from another screen keeps its
    // prompts piled at one spot with no label.
    if (!(prompt.icon > 0 && prompt.icon < 200 && prompt.label > 0 && prompt.label < 1000)) continue;
    // Its label moved off the screen (ShowSelectLabel: SELECT where a click on the item does it): not there.
    if (const uint32_t label = NthChild(base, inner, 0); label && std::fabs(Float(base, label + 0x10) - 4000.0f) < 1) continue;
    char name[128] = {};
    if (!Readable(base, path, sizeof(name) - 1)) continue;
    std::memcpy(name, base + path, sizeof(name) - 1);
    prompt.buttons = input::PromptButtons(std::string_view(name, strnlen(name, sizeof(name) - 1)));
    // The mouse in use: no icons (PromptSwap clears them), but for BACK where a right click does it; the label
    // alone takes the click.
    prompt.icon_hidden = input::MouseLast() && !(prompt.buttons == kButtonBBit && g_sidebar_back_icon);
    if (prompt.buttons) prompts.push_back(prompt);
  }
  return prompts;
}

// The home button (the key and a house at the bottom right of the screens deep in the menus) is a group of two
// icons, the house (first, scale 0.22) and the key (second, 0.32), each centred on its x and 100 units wide at
// scale 1. It sits by the screen's prompt bar in one of two places: the first child of the root element's third
// child (livery editor, options), or a sibling of the root (paint, my cars). Its own opacity (+0xE0) is 1 where
// the screen has a home and 0 where it has none (quick race).
constexpr int kHomeGroup = 2;
constexpr uint32_t kOwnOpacity = 0xE0;
constexpr float kHomeIconHalf = 50, kHomeMarginX = 6, kHomeHalfHeight = 18;
struct HomeButton {
  float min_x = 0, max_x = 0, min_y = 0, max_y = 0;
  bool Hit(input::UiPoint p) const { return p.x >= min_x && p.x <= max_x && p.y >= min_y && p.y <= max_y; }
};

bool HomeShaped(const uint8_t* base, uint32_t group) {
  const uint32_t house = NthChild(base, group, 0), key = NthChild(base, group, 1);
  return house && key && !NthChild(base, group, 2) && std::fabs(Float(base, house + kElementScale) - 0.22f) < 0.03f &&
         std::fabs(Float(base, key + kElementScale) - 0.32f) < 0.03f;
}

// The home group the screen shows by this bar, or 0.
uint32_t HomeIcons(const uint8_t* base, uint32_t bar) {
  const uint32_t root = ElementOf(base, Word(base, bar + kItemObject));
  if (!GuestPointer(root)) return 0;
  const auto shown = [&](uint32_t group) {
    return group && HomeShaped(base, group) && Float(base, group + kOwnOpacity) > 0.5f;
  };
  const uint32_t inside = NthChild(base, NthChild(base, root, kHomeGroup), 0);
  if (shown(inside)) return inside;
  uint32_t sibling = NthChild(base, Word(base, root + kParent), 0);
  for (int n = 0; GuestPointer(sibling) && n < 16; ++n, sibling = Word(base, sibling + kSibling)) {
    if (sibling != root && shown(sibling)) return sibling;
  }
  return 0;
}

std::optional<HomeButton> ReadHome(const uint8_t* base, uint32_t bar) {
  if (!GuestPointer(bar) || Word(base, bar) != kBarVtable) return std::nullopt;
  const uint32_t icons = HomeIcons(base, bar);
  if (!icons) return std::nullopt;
  uint32_t up = icons;
  for (int n = 0; GuestPointer(up) && n < 32; ++n, up = Word(base, up + kParent)) {
    if (!Visible(base, up)) return std::nullopt;
  }
  const uint32_t key = NthChild(base, icons, 1);
  HomeButton home;
  int count = 0;
  for (uint32_t icon = Word(base, icons + kChild); GuestPointer(icon) && count < 4; icon = Word(base, icon + kSibling)) {
    if (icon == key && input::MouseLast()) continue;  // the key's icon is clear with the mouse in use
    const float x = Float(base, icon + kWorldX), y = Float(base, icon + kWorldY);
    const float half = kHomeIconHalf * std::fabs(Float(base, icon + kElementScale));
    if (!(std::fabs(x) <= 640 && y < input::kBottomRowY && half > 0 && half < 60)) continue;
    home.min_x = count ? std::min(home.min_x, x - half) : x - half;
    home.max_x = count ? std::max(home.max_x, x + half) : x + half;
    home.min_y = y - kHomeHalfHeight, home.max_y = y + kHomeHalfHeight;
    ++count;
  }
  if (count == 0) return std::nullopt;
  home.min_x -= kHomeMarginX, home.max_x += kHomeMarginX;
  return home;
}

using fm4_guest::kHelpButtonVtable;

uint32_t PromptElement(const uint8_t* base, uint32_t button) {
  const uint32_t object = Word(base, button + kItemObject);
  const uint32_t node = GuestPointer(object) ? Word(base, object + kObjectNode) : 0;
  return GuestPointer(node) ? Word(base, node + kNodeElement) : 0;
}

// A tab header's key letters (Q / E, LB / RB, beside its arrows) mean nothing to the mouse: hidden while it is in
// use, shown again by a key or the pad. A header's root element holds the three names (first), the two letters
// (texts, second and third) and the two arrows (fourth and fifth). They never move, so hiding them is safe. Only
// letters hidden here are shown again, each checked to be its list's still (game thread only).
constexpr uint32_t kElementType = 0x0A;
constexpr uint8_t kElementGroup = 1, kElementText = 5;
struct HiddenTabKey {
  uint32_t list = 0, element = 0;
};
std::vector<HiddenTabKey> g_hidden_tab_keys;

bool TabKeyLetters(const uint8_t* base, uint32_t list, uint32_t& left, uint32_t& right) {
  if (!GuestPointer(list) || Word(base, list) != kListVtable) return false;
  const uint32_t begin = Word(base, list + kItemsBegin), end = Word(base, list + kItemsEnd);
  if (!GuestPointer(begin) || end - begin != 12) return false;
  const uint32_t root = ElementOf(base, Word(base, list + kItemObject));
  const uint32_t names = NthChild(base, root, 0);
  left = NthChild(base, root, 1), right = NthChild(base, root, 2);
  const uint32_t left_arrow = NthChild(base, root, 3), right_arrow = NthChild(base, root, 4);
  return names && NthChild(base, names, 2) && left && right && left_arrow && right_arrow &&
         Byte(base, left + kElementType) == kElementText && Byte(base, right + kElementType) == kElementText &&
         Byte(base, left_arrow + kElementType) == kElementGroup && Byte(base, right_arrow + kElementType) == kElementGroup &&
         Float(base, left + kWorldX) < Float(base, right + kWorldX);
}

void UpdateTabKeys(uint8_t* base, const std::vector<uint32_t>& lists) {
  const bool mouse = input::MouseLast();
  std::erase_if(g_hidden_tab_keys, [&](const HiddenTabKey& hidden) {
    uint32_t left = 0, right = 0;
    const bool same = TabKeyLetters(base, hidden.list, left, right) && (hidden.element == left || hidden.element == right);
    if (same && !mouse) base[hidden.element + kVisible] = 1;
    return !same || !mouse;
  });
  if (!mouse) return;
  for (const uint32_t list : lists) {
    uint32_t left = 0, right = 0;
    if (!TabKeyLetters(base, list, left, right)) continue;
    for (const uint32_t letter : {left, right}) {
      if (!Visible(base, letter)) continue;
      base[letter + kVisible] = 0;
      g_hidden_tab_keys.push_back({list, letter});
    }
  }
}

struct Layer {
  uint32_t frame = 0;
  int layer = -1;
};

// A paint screen with the car on show: a colour choice (CLiveryColorSelect, CCarColorSelect), the livery
// editor's layers (CLiveryLayerSelect) or a layer's placement.
bool CarViewUp() {
  const uint8_t* const base = g_base;
  if (!base) return false;
  if (const uint32_t color = g_livery_color; GuestPointer(color) && Word(base, color) == kLiveryColorSelectVtable) {
    return true;
  }
  if (const uint32_t layers = g_livery_layers;
      GuestPointer(layers) && Word(base, layers) == kLiveryLayerSelectVtable) {
    return true;
  }
  if (const uint32_t livery = g_livery; GuestPointer(livery) && Word(base, livery) == kLiveryPlacementVtable &&
                                         Word(base, livery + 4) == kLiveryPlacementVtable2) {
    return true;
  }
  std::lock_guard lock(g_containers_mutex);
  return std::any_of(g_idle_screens.begin(), g_idle_screens.end(),
                     [&](uint32_t screen) { return GuestPointer(screen) && Word(base, screen) == kCarColorVtable; });
}

Layer LayerOf(const uint8_t* base, uint32_t container) {
  if (SliderGroup(base, container)) container = Word(base, Word(base, container + kGroupControlsBegin));
  const uint32_t object = Word(base, container + kItemObject);
  const uint32_t scene = GuestPointer(object) ? Word(base, object + kObjectScene) : 0;
  if (!GuestPointer(scene)) return {};
  return {Word(base, scene + kSceneFrame) & 0xFFFFFF, int(Byte(base, scene + kSceneLayer))};
}

// Calls an object's virtual function (the address at `offset` in its vtable, from the game's image) with
// one argument.
bool CallVirtual(PPCContext& ctx, uint8_t* base, uint32_t stack, uint32_t object, uint32_t offset, uint32_t arg) {
  if (!GuestPointer(object)) return false;
  const uint32_t vtable = Word(base, object);
  const uint32_t function = vtable >= 0x82000000 && vtable < 0x83000000 ? Word(base, vtable + offset) : 0;
  if (function < 0x82000000 || function >= 0x83000000) return false;
  return CallGuest(ctx, base, stack, function, [&](PPCContext& c) {
    c.r3.u64 = object;
    c.r4.u64 = arg;
  });
}

// Lights the focused item again, when it is still the one turned off (`only`: that container; 0: all).
void Relight(PPCContext& ctx, uint8_t* base, uint32_t stack, uint32_t only = 0) {
  for (const Unlit& u : g_unlit) {
    if (only != 0 && u.container != only) continue;
    // The item focused now (a list that came back to the screen can have another one than was turned off).
    const auto list = ReadList(base, u.container);
    if (!list) continue;
    for (size_t i = 0; i < list->items.size(); ++i) {
      const input::MenuItem& item = list->items[i];
      if (!item.shown || item.id == 0 || input::EntryOf(*list, int(i)) != list->focus) continue;
      const bool alt = list->grid_rows > 0 || Byte(base, u.container + kListLightAlt) != 0;
      CallVirtual(ctx, base, stack, item.id, alt ? kItemLightAlt : kItemLight, 0);
      break;
    }
  }
  if (only != 0) {
    Forget(only);
  } else {
    g_unlit.clear();
    g_any_unlit = false;
  }
}

// The game draws a prompt's label 1.6 units above the centre of its icon. Each label is put level with the icons
// once, through the game's own property setter (0x8298DF70: y; a write to the element is not drawn). The button's
// +0xB0 holds the label's element (holder +4 -> +4).
using fm4_guest::kSetElementY;
constexpr uint32_t kPromptLabelHolder = 0xB0;
constexpr float kLabelGameY = 1.0f, kLabelLevelY = -0.6f;

void LevelPromptLabels(PPCContext& ctx, uint8_t* base, uint32_t stack, const std::vector<uint32_t>& bars) {
  for (const uint32_t bar : bars) {
    const uint32_t begin = Word(base, bar + kPromptsBegin), end = Word(base, bar + kPromptsEnd);
    if (!GuestPointer(begin) || end < begin || (end - begin) / kPromptStride > 16) continue;
    for (uint32_t p = begin; p + kPromptStride <= end; p += kPromptStride) {
      const uint32_t button = Word(base, p);
      if (!GuestPointer(button) || Word(base, button) != kHelpButtonVtable) continue;
      const uint32_t label = NthChild(base, NthChild(base, PromptElement(base, button), 0), 0);
      const uint32_t holder = button + kPromptLabelHolder;
      const uint32_t target = Word(base, holder + 4);
      if (!label || !GuestPointer(target) || Word(base, target + 4) != label) continue;
      if (std::fabs(Float(base, label + 0x10) - kLabelGameY) > 0.01f) continue;
      CallGuest(ctx, base, stack, kSetElementY, [&](PPCContext& c) {
        c.r3.u64 = holder;
        c.f1.f64 = kLabelLevelY;
      });
    }
  }
}

// SELECT says nothing to the mouse where a click on the item does it: its label moves off the screen, with the game's
// own setter (text ignores its element's opacity), and the bar's layout stays as it is. A dialog's OK is its button.
constexpr float kLabelGoneY = 4000;
// A side menu's bar is SELECT and BACK alone: with SELECT gone, BACK takes its slot (the prompt's inner group moves,
// with the game's setter on the button's holder for it; the bar's own layout is left as it is).
using fm4_guest::kSetElementX;
constexpr uint32_t kPromptInnerHolder = 0xA4, kLocalX = 0x0C;
void ShowSelectLabel(PPCContext& ctx, uint8_t* base, uint32_t stack, uint32_t bar, bool hide) {
  const uint32_t begin = Word(base, bar + kPromptsBegin), end = Word(base, bar + kPromptsEnd);
  if (!GuestPointer(begin) || end < begin || (end - begin) / kPromptStride > 16) return;
  uint32_t select = 0, back = 0;
  int shown = 0;
  for (uint32_t p = begin; p + kPromptStride <= end; p += kPromptStride) {
    const uint32_t button = Word(base, p);
    if (!GuestPointer(button) || Word(base, button) != kHelpButtonVtable) continue;
    const uint32_t data = Word(base, button + kPromptData);
    const uint32_t path = GuestPointer(data) ? Word(base, data + kDataIconPath) : 0;
    char name[128] = {};
    if (!GuestPointer(path) || !Readable(base, path, sizeof(name) - 1)) continue;
    std::memcpy(name, base + path, sizeof(name) - 1);
    const uint32_t buttons = input::PromptButtons(std::string_view(name, strnlen(name, sizeof(name) - 1)));
    const bool on_screen = ShownOnScreen(base, PromptElement(base, button));
    shown += on_screen ? 1 : 0;
    if (buttons == kButtonBBit && (on_screen || !back)) back = button;
    if (buttons == kButtonABit && (on_screen || !select)) select = button;
    const uint32_t element = PromptElement(base, button);
    // The bar reuses its buttons from screen to screen: one that was SELECT or a moved BACK may now be anything.
    if (buttons != kButtonBBit) {
      const uint32_t moved = NthChild(base, element, 0);
      const uint32_t moved_holder = button + kPromptInnerHolder;
      const uint32_t moved_target = Word(base, moved_holder + 4);
      if (moved && GuestPointer(moved_target) && Word(base, moved_target + 4) == moved &&
          std::fabs(Float(base, moved + kLocalX)) >= 0.5f) {
        CallGuest(ctx, base, stack, kSetElementX, [&](PPCContext& c) {
          c.r3.u64 = moved_holder;
          c.f1.f64 = 0;
        });
      }
    }
    const uint32_t label = NthChild(base, NthChild(base, element, 0), 0);
    if (!label) continue;
    const uint32_t holder = button + kPromptLabelHolder;
    const uint32_t target = Word(base, holder + 4);
    if (!GuestPointer(target) || Word(base, target + 4) != label) continue;
    const float now = Float(base, label + 0x10);
    const bool gone = std::fabs(now - kLabelGoneY) < 1;
    const bool want_gone = buttons == kButtonABit && hide && Float(base, element + kWorldY) <= input::kBottomRowY;
    if (gone == want_gone) continue;
    CallGuest(ctx, base, stack, kSetElementY, [&](PPCContext& c) {
      c.r3.u64 = holder;
      c.f1.f64 = want_gone ? kLabelGoneY : kLabelLevelY;
    });
  }
  if (!back) return;
  const uint32_t back_root = PromptElement(base, back), inner = NthChild(base, back_root, 0);
  const uint32_t holder = back + kPromptInnerHolder;
  const uint32_t target = Word(base, holder + 4);
  if (!inner || !GuestPointer(target) || Word(base, target + 4) != inner) return;
  float want = 0;
  if (hide && select && shown == 2) {
    const uint32_t select_root = PromptElement(base, select);
    const float select_x = Float(base, select_root + kWorldX), back_x = Float(base, back_root + kWorldX);
    if (select_x < 0 && select_x < back_x && Float(base, select_root + kWorldY) <= input::kBottomRowY) {
      want = select_x - back_x;
    }
  } else if (hide && shown != 2 && shown != 0) {
    want = 0;
  } else if (hide) {
    return;  // the bar between layouts: BACK stays where it is until it is back
  }
  if (std::fabs(Float(base, inner + kLocalX) - want) < 0.5f) return;
  CallGuest(ctx, base, stack, kSetElementX, [&](PPCContext& c) {
    c.r3.u64 = holder;
    c.f1.f64 = want;
  });
}

// The pointer's prompt highlight: the box last reported, and who draws it.
std::mutex g_highlight_mutex;
std::optional<input::UiRect> g_highlight;
std::function<void(std::optional<input::UiRect>)> g_highlight_listener;

void NoteHighlight(std::optional<input::UiRect> box) {
  std::function<void(std::optional<input::UiRect>)> listener;
  {
    std::lock_guard lock(g_highlight_mutex);
    if (g_highlight == box) return;
    g_highlight = box;
    listener = g_highlight_listener;
  }
  if (listener) listener(box);
}

bool PlainMenuItem(uint32_t vtable);

// A strip's item the pointer is on (a carousel's side item, a half-shown row): moving the focus there would
// scroll the strip under the pointer, so it is lit for the look only and the focus stays; the focused item is
// let go meanwhile and lit again when the pointer leaves (game thread only).
struct HoverLit {
  uint32_t container = 0, object = 0;
} g_hover;
// A click on such an item: the focus goes there (the strip scrolls) and the item is chosen once it is there.
struct PendingChoose {
  uint32_t container = 0;
  int entry = -1;
  double since = 0, reached = 0;
} g_choose;
constexpr double kChooseSettle = 0.3, kChooseGiveUp = 2.5;  // seconds
// A click on a carousel's side tile only brings it to the centre. It slides away from the pointer, so until the
// pointer moves on, the next click there opens the tile it brought in, not the one that slid under it.
struct BroughtIn {
  uint32_t container = 0;
  int entry = -1;
  input::UiPoint at;
  double when = 0;
} g_brought;
constexpr float kBroughtSlop = 14;  // UI units the pointer may drift between the two clicks
constexpr double kStepClicks = 0.45;  // seconds: clicks closer than this keep stepping (a fast run through the strip)

// The pointer moves the focus at most this often (the focus sound plays on each move).
constexpr double kHoverGap = 0.07;
double g_hover_moved = 0;
bool g_hover_pending = false;  // a move held back: taken on a later frame, even with the mouse still

bool LightAlt(const uint8_t* base, uint32_t container, const input::MenuList& list) {
  return list.grid_rows > 0 || Byte(base, container + kListLightAlt) != 0;
}

uint32_t FocusedObject(const input::MenuList& list) {
  for (size_t i = 0; i < list.items.size(); ++i) {
    if (list.items[i].shown && input::EntryOf(list, int(i)) == list.focus) return list.items[i].id;
  }
  return 0;
}

void EndHover(PPCContext& ctx, uint8_t* base, uint32_t stack) {
  if (!g_hover.container) return;
  const HoverLit hover = std::exchange(g_hover, HoverLit{});
  CallVirtual(ctx, base, stack, hover.object, kItemLetGo, 0);
  const auto list = ReadList(base, hover.container);
  const uint32_t focused = list ? FocusedObject(*list) : 0;
  if (focused) CallVirtual(ctx, base, stack, focused, LightAlt(base, hover.container, *list) ? kItemLightAlt : kItemLight, 0);
}

void HoverItem(PPCContext& ctx, uint8_t* base, uint32_t stack, uint32_t container, const input::MenuList& list,
               int item) {
  const uint32_t object = list.items[size_t(item)].id;
  if (!object || (g_hover.container == container && g_hover.object == object)) return;
  // Only items whose highlight is theirs alone (PlainMenuItem): a carousel's car tiles keep the slot's
  // highlight whatever they are told, and letting go of a colour swatch hides its screen's UI.
  const uint32_t focused_now = FocusedObject(list);
  if (!PlainMenuItem(Word(base, object)) || (focused_now && !PlainMenuItem(Word(base, focused_now)))) return;
  if (g_hover.container && g_hover.container != container) EndHover(ctx, base, stack);
  if (g_hover.container) {
    CallVirtual(ctx, base, stack, g_hover.object, kItemLetGo, 0);  // the last one hovered
  } else if (const uint32_t focused = FocusedObject(list); focused && focused != object) {
    CallVirtual(ctx, base, stack, focused, kItemLetGo, 0);
  }
  CallVirtual(ctx, base, stack, object, LightAlt(base, container, list) ? kItemLightAlt : kItemLight, 0);
  g_hover = {container, object};
}

// A clicked strip item: chosen (A) once the focus has been on it a moment (the strip done scrolling).
void PlayPendingChoose(const uint8_t* base) {
  if (!g_choose.container) return;
  const double now = NowSeconds();
  const auto list = ReadList(base, g_choose.container);
  if (!list || now - g_choose.since > kChooseGiveUp) {
    g_choose = {};
    return;
  }
  if (list->focus != g_choose.entry) {
    g_choose.reached = 0;
    return;
  }
  if (g_choose.reached == 0) g_choose.reached = now;
  if (now - g_choose.reached >= kChooseSettle && !input::MenuWheelTaps().Busy()) {
    g_choose = {};
    input::MenuWheelTaps().AddClick(false);
  }
}

// The livery placement screen when it is up: its editor placing a layer and its tab list on screen.
struct Livery {
  uint32_t screen = 0, editor = 0;
  int mode = -1;
  float tabs_top = 0;  // the tab row's top (UI y): the car is above it
};

std::optional<Livery> LiveryUp(PPCContext& ctx, uint8_t* base, uint32_t stack) {
  const uint32_t screen = g_livery;
  if (!GuestPointer(screen) || Word(base, screen) != kLiveryPlacementVtable ||
      Word(base, screen + 4) != kLiveryPlacementVtable2) {
    return std::nullopt;
  }
  Livery out{screen, Word(base, screen + kLiveryEditor)};
  if (!GuestPointer(out.editor)) return std::nullopt;
  const uint32_t state = Word(base, out.editor + kLiveryEditorState);
  if (state < 2 || state > 4) return std::nullopt;
  const uint32_t tabs = Word(base, screen + kLiveryTabs);
  const auto list = ReadList(base, tabs);
  if (!list) return std::nullopt;
  bool any = false;
  for (const input::MenuItem& item : list->items) {
    if (!item.shown) continue;
    out.tabs_top = any ? std::max(out.tabs_top, item.max_y) : item.max_y;
    any = true;
  }
  if (!any) return std::nullopt;
  // The tab's mode, as the screen reads it.
  const uint32_t data = Word(base, screen + kLiveryTabData);
  const uint32_t vtable = GuestPointer(data) ? Word(base, data) : 0;
  const uint32_t get = vtable >= 0x82000000 && vtable < 0x83000000 ? Word(base, vtable + 8) : 0;
  if (get < 0x82000000 || get >= 0x83000000) return std::nullopt;
  uint32_t tab = 0;
  CallGuest(ctx, base, stack, get, [&](PPCContext& c) {
    c.r3.u64 = data;
    c.r4.u64 = uint32_t(list->focus);
    c.r5.u64 = 0;
  }, nullptr, &tab);
  if (!GuestPointer(tab)) return std::nullopt;
  out.mode = int32_t(Word(base, tab + 4));
  return out;
}

// The top of the panel along the bottom (tabs, readouts): the highest shown item of any list in the lower half
// of the screen. The car is above it.
float LiveryPanelTop(const uint8_t* base, const Livery& livery) {
  std::vector<uint32_t> containers;
  {
    std::lock_guard lock(g_containers_mutex);
    containers.assign(g_containers.begin(), g_containers.end());
  }
  float top = livery.tabs_top;
  for (uint32_t container : containers) {
    const uint32_t vtable = Word(base, container);
    if (vtable != kListVtable && vtable != kGridVtable) continue;
    const auto list = ReadList(base, container);
    if (!list) continue;
    for (const input::MenuItem& item : list->items) {
      if (item.shown && item.max_y < 0) top = std::max(top, item.max_y);
    }
  }
  return top;
}

void LiveryAdjust(PPCContext& ctx, uint8_t* base, uint32_t stack, const Livery& livery, int mode, double x,
                  double y) {
  // As the stick does: a group (the vinyl group editor, or layers selected together) is adjusted whole.
  uint32_t group = 0, changed = 0;
  CallGuest(ctx, base, stack, kLiveryEditsGroup, [&](PPCContext& c) { c.r3.u64 = livery.editor; }, nullptr, &group);
  CallGuest(ctx, base, stack, (group & 0xFF) ? kLiveryAdjustGroup : kLiveryAdjust, [&](PPCContext& c) {
    c.r3.u64 = livery.editor;
    c.r4.u64 = uint32_t(mode);
    c.f1.f64 = x;
    c.f2.f64 = y;
    c.r7.u64 = 0;
  }, nullptr, &changed);
  if ((changed & 0xFF) != 0) {
    CallGuest(ctx, base, stack, kLiveryShowChange, [&](PPCContext& c) { c.r3.u64 = livery.screen; });
    CallGuest(ctx, base, stack, kLiveryChanged, [&](PPCContext& c) { c.r3.u64 = livery.screen; });
  }
  if (REXCVAR_GET(fm4_menu_mouse_log)) REXLOG_INFO("Menu mouse: livery mode {} by {:.3f} {:.3f}", mode, x, y);
}

// Pointer travel (UI units) to the screen's input for each mode: MOVE follows the pointer (the side view shows
// about 1.2 UI units per layer unit), the others turn with horizontal travel (SIZE: x and y).
constexpr double kLiveryDragGain[5] = {1.0 / 1.2, 0.005, 0.5, 0.005, 0.003};
// A wheel notch: SIZE both ways, Shift: SPIN.
constexpr double kLiveryWheelSize = 0.05, kLiveryWheelSpin = 5.0;
constexpr int kLiveryMove = 0, kLiverySize = 1, kLiverySpin = 2;

// The mouse is in use: the career hub's idle time starts again (see kMainMenuIdleTimer), at most 4 times a
// second. Returns how long the hub had been idle (seconds), NaN when there is no hub or it was not looked at.
// The mouse used on a screen that hides its menu when idle: a hidden menu comes back, a shown one restarts
// its idle (as a press does; the hub has its own, KeepAwake).
void WakeIdleScreens(PPCContext& ctx, uint8_t* base, uint32_t stack) {
  std::vector<uint32_t> screens;
  {
    std::lock_guard lock(g_containers_mutex);
    screens.assign(g_idle_screens.begin(), g_idle_screens.end());
  }
  for (const uint32_t screen : screens) {
    const uint32_t vtable = Word(base, screen);
    if (vtable == kPreRaceVtable) {
      const bool hidden = base[screen + kPreRaceHidden] != 0;
      CallGuest(ctx, base, stack, hidden ? kPreRaceShow : kTimerRestart,
                [&](PPCContext& c) { c.r3.u64 = hidden ? screen : screen + kPreRaceTimer; });
      if (hidden && REXCVAR_GET(fm4_menu_mouse_log)) REXLOG_INFO("Menu mouse: screen {:08X} woken", screen);
      continue;
    }
    if (vtable != kCarColorVtable) continue;
    if (base[screen + kIdleHidden] != 0) {
      CallGuest(ctx, base, stack, kScreenWake, [&](PPCContext& c) { c.r3.u64 = screen; });
      if (REXCVAR_GET(fm4_menu_mouse_log)) REXLOG_INFO("Menu mouse: screen {:08X} woken", screen);
    } else if (Word(base, screen + kIdleSince) != 0) {
      uint32_t ticks = 0;
      CallGuest(ctx, base, stack, kGameTicks, [](PPCContext&) {}, nullptr, &ticks);
      if (ticks != 0) StoreWord(base, screen + kIdleSince, ticks);
    }
  }
}

constexpr double kHubHiding = 7.7;  // seconds idle: just before the hub starts hiding its menu (measured ~8)

double KeepAwake(PPCContext& ctx, uint8_t* base, uint32_t stack) {
  constexpr double kNone = std::numeric_limits<double>::quiet_NaN();
  const auto now = Clock::now();
  if (now - g_last_keep < std::chrono::milliseconds(250)) return kNone;
  g_last_keep = now;
  const uint32_t menu = g_main_menu;
  if (!GuestPointer(menu) || Word(base, menu) != kMainMenuVtable ||
      Word(base, menu + kMainMenuInputAt) != kMainMenuInputVtable) {
    return kNone;
  }
  const uint32_t timer = menu + kMainMenuIdleTimer;
  const uint32_t clock = Word(base, timer + kTimerClock);
  if (clock < 0x82000000 || clock >= 0x83000000 || !Readable(base, timer + kTimerSince, 8)) return kNone;
  double seconds = kNone;
  CallGuest(ctx, base, stack, clock, [&](PPCContext& c) { c.r3.u64 = timer; }, &seconds);
  if (!std::isfinite(seconds)) return kNone;
  const double idle = seconds - Double(base, timer + kTimerSince);
  uint64_t bits;
  std::memcpy(&bits, &seconds, 8);
  bits = __builtin_bswap64(bits);
  std::memcpy(base + timer + kTimerSince, &bits, 8);
  return idle;
}

// Whether lighting an item would scroll its list: a carousel's (any item but the focused one), or one
// outside the entries a list shows (a half-shown item at its end).
bool FocusScrolls(const uint8_t* base, uint32_t container, const input::MenuList& list, int item) {
  if (list.grid_rows > 0) return false;  // the cells under the pointer are the ones on screen
  if (SliderGroup(base, container)) return false;  // a group's sliders are all on screen
  const int entry = input::EntryOf(list, item);
  if (entry == list.focus) return false;
  if (Word(base, container + kListFocusSlot) != 0xFFFFFFFFu) return true;
  const int first = int32_t(Word(base, container + kListFirstShown));
  const int count = int32_t(Word(base, container + kListShownCount));
  return count > 0 && first >= 0 && (entry < first || entry >= first + count);
}

// Takes the focus straight to an item, as the game moves it (see kGridGoTo). True when it is there now.
// The pointer on a list its screen's focus group does not work now (the livery colour screen opens on its
// colour box): the group moves to it, as the D-pad would.
void FocusGroupOn(PPCContext& ctx, uint8_t* base, uint32_t stack, uint32_t container) {
  uint32_t group = 0;
  {
    std::lock_guard lock(g_containers_mutex);
    for (const uint32_t g : g_groups) {
      if (Word(base, g) != kGroupVtable) continue;
      const uint32_t first = Word(base, g + kGroupControlsBegin), last = Word(base, g + kGroupControlsEnd);
      if (!GuestPointer(first) || last < first || last - first > 0x100) continue;
      for (uint32_t at = first; at < last; at += 4) {
        if (Word(base, at) == container) group = g;
      }
      if (group) break;
    }
  }
  if (!group || Word(base, group + kGroupFocused) == container) return;
  CallGuest(ctx, base, stack, kGroupSetFocus, [&](PPCContext& c) {
    c.r3.u64 = group;
    c.r4.u64 = container;
    c.r5.u64 = 1;
  });
  if (REXCVAR_GET(fm4_menu_mouse_log)) REXLOG_INFO("Menu mouse: focus group {:08X} to {:08X}", group, container);
}

bool FocusItem(PPCContext& ctx, uint8_t* base, uint32_t stack, uint32_t container, const input::MenuList& list,
               int item) {
  const int entry = input::EntryOf(list, item);
  if (entry < 0 || list.focus < 0) return false;
  if (SliderGroup(base, container)) {
    const uint32_t slider = list.items[size_t(item)].id;
    if (Word(base, container + kGroupFocused) != slider) {
      CallGuest(ctx, base, stack, kGroupSetFocus, [&](PPCContext& c) {
        c.r3.u64 = container;
        c.r4.u64 = slider;
        c.r5.u64 = 1;
      });
    }
    return Word(base, container + kGroupFocused) == slider;
  }
  FocusGroupOn(ctx, base, stack, container);
  if (entry == list.focus) {
    Relight(ctx, base, stack, container);
    return true;
  }
  if (list.grid_rows > 0) {
    CallGuest(ctx, base, stack, kGridGoTo, [&](PPCContext& c) {
      c.r3.u64 = container;
      c.r4.u64 = uint32_t(entry);
    });
    const bool there = int32_t(Word(base, container + kGridFocus)) == entry;
    if (there) Forget(container);
    return there;
  }
  const bool forward = entry > list.focus;
  const uint32_t vtable = Word(base, container);
  if (const uint32_t move = Word(base, vtable + kListMoveEvent); move >= 0x82000000 && move < 0x83000000) {
    CallGuest(ctx, base, stack, move, [&](PPCContext& c) {
      c.r3.u64 = container;
      c.r4.u64 = Word(base, forward ? kMoveForward : kMoveBack);
    });
  }
  CallGuest(ctx, base, stack, kListSetFocus, [&](PPCContext& c) {
    c.r3.u64 = container;
    c.r4.u64 = uint32_t(entry);
    c.r5.u64 = 0;
  });
  const bool there = int32_t(Word(base, container + kFocus)) == entry;
  if (there) Forget(container);
  return there;
}

// Turns the focused item's highlight off (the pointer is on no item). The focus itself stays.
// Item classes whose highlight can be turned off without the screen reacting: plain menu buttons and rows.
// (Letting go of other items tells their screen the focus left them: the car colour picker then hides its UI;
// swatches, cars and grid cells keep their highlight.)
using fm4_guest::kButtonVtable;
using fm4_guest::kButtonTextVtable;
using fm4_guest::kButtonTextureVtable;
using fm4_guest::kButtonTextAndTextureVtable;
bool PlainMenuItem(uint32_t vtable) {
  static constexpr uint32_t kPlain[] = {
      kButtonVtable,  // CButton
      kButtonTextVtable,  // CButtonText
      kButtonTextureVtable,  // CButtonTexture
      kButtonTextAndTextureVtable,  // CButtonTextAndTexture
      kSpinnerVtable,  // CSpinner
      kSliderVtable,  // CSlider
      kTuneSliderVtable,  // CTuneSlider
  };
  return std::find(std::begin(kPlain), std::end(kPlain), vtable) != std::end(kPlain);
}

void Unlight(PPCContext& ctx, uint8_t* base, uint32_t stack, uint32_t container, const input::MenuList& list) {
  if (SliderGroup(base, container)) return;
  for (size_t i = 0; i < list.items.size(); ++i) {
    const input::MenuItem& item = list.items[i];
    if (!item.shown || item.id == 0 || input::EntryOf(list, int(i)) != list.focus) continue;
    if (!PlainMenuItem(Word(base, item.id))) {
      static uint32_t logged = 0;
      if (REXCVAR_GET(fm4_menu_mouse_log) && std::exchange(logged, Word(base, item.id)) != Word(base, item.id)) {
        REXLOG_INFO("Menu mouse: item class {:08X} stays lit", Word(base, item.id));
      }
      return;
    }
    // Already off (the same item)? A list back on screen may have lit another one since.
    const auto known = std::find_if(g_unlit.begin(), g_unlit.end(), [&](const Unlit& u) { return u.container == container; });
    if (known != g_unlit.end() && known->object == item.id) return;
    if (CallVirtual(ctx, base, stack, item.id, kItemLetGo, 0)) {
      if (known != g_unlit.end()) {
        known->object = item.id;
      } else {
        g_unlit.push_back({container, item.id});
      }
      g_any_unlit = true;
    }
    return;
  }
}

}  // namespace

void PointerMoved(std::optional<input::UiPoint> point) {
  if (point) {
    std::lock_guard lock(g_autovista_mutex);
    g_autovista.Move(*point);
    g_car_view.Move(*point);
  }
  Shared& state = State();
  std::lock_guard lock(state.mutex);
  state.pointer = point;
  state.moved = true;
}

void LeftClick() {
  Shared& state = State();
  std::lock_guard lock(state.mutex);
  state.held = true;
  // In the Autovista viewer's picture a press may start a walk: its click comes on the release.
  if (InAutovistaPicture(state.pointer)) {
    std::lock_guard autovista(g_autovista_mutex);
    g_autovista.Press(false, *state.pointer);
    return;
  }
  state.click = true;
}

void RightClick() {
  Shared& state = State();
  std::lock_guard lock(state.mutex);
  if (REXCVAR_GET(fm4_menu_mouse_log)) {
    REXLOG_INFO("Menu mouse: right press at {} {}, car view {}", state.pointer ? state.pointer->x : -1.f,
                state.pointer ? state.pointer->y : -1.f, CarViewUp());
  }
  if (InAutovistaPicture(state.pointer)) {
    std::lock_guard autovista(g_autovista_mutex);
    g_autovista.Press(true, *state.pointer);
    return;
  }
  // Over a paint screen's car: a drag turns the camera; let go without one, it is back (on the release).
  if (OverCarView(state.pointer)) {
    std::lock_guard autovista(g_autovista_mutex);
    g_car_view.Press(true, *state.pointer);
    return;
  }
  ++state.right_clicks;
}

void WheelNotches(int notches, bool sideways) {
  if (AutovistaUp() && !g_viewer_list_up) {
    std::lock_guard lock(g_autovista_mutex);
    g_autovista.Notches(notches);
    return;
  }
  Shared& state = State();
  std::lock_guard lock(state.mutex);
  (sideways ? state.sideways_notches : state.notches) += notches;
}

void LeftRelease() {
  bool click = false;
  {
    std::lock_guard lock(g_autovista_mutex);
    click = g_autovista.Release(false);
  }
  Shared& state = State();
  std::lock_guard lock(state.mutex);
  state.held = false;
  if (click) state.click = true;
}

void RightRelease() {
  bool click = false;
  {
    std::lock_guard lock(g_autovista_mutex);
    click = g_autovista.Release(true);
    click = g_car_view.Release(true) || click;
  }
  if (!click) return;
  Shared& state = State();
  std::lock_guard lock(state.mutex);
  ++state.right_clicks;
}

void Reset() {
  NoteHighlight(std::nullopt);
  {
    std::lock_guard lock(g_autovista_mutex);
    g_autovista.Reset();
    g_car_view.Reset();
  }
  Shared& state = State();
  std::lock_guard lock(state.mutex);
  state.pointer.reset();
  state.moved = state.click = state.held = false;
  state.right_clicks = state.notches = state.sideways_notches = 0;
  StopDrag();
}

void AutovistaTick(uint32_t camera) {
  g_autovista_camera = camera;
  g_autovista_tick_ms.store(SteadyMs(), std::memory_order_relaxed);
}

void PhotoModeStarted(uint32_t camera) {
  g_photo_camera = camera;
}

void RaceSubmenuCreated(uint32_t scene) { g_race_submenu = scene; }

void RaceSubmenuDestroyed(uint32_t scene) {
  uint32_t expected = scene;
  if (g_race_submenu.compare_exchange_strong(expected, 0)) g_race_photo = false;
}

bool TitleScreenUp() { return g_title_up.load(std::memory_order_relaxed); }

bool SidebarBackIcon() { return g_sidebar_back_icon.load(std::memory_order_relaxed); }

void AutovistaSticks(rex::input::X_INPUT_GAMEPAD& pad, double elapsed) {
  input::Sticks sticks;
  {
    std::lock_guard lock(g_autovista_mutex);
    if (!AutovistaUp() || g_viewer_list_up) {
      g_autovista.Reset();
      // A paint screen's car turned by a right drag (only the right stick: the left one works the screen).
      g_car_view.SetPhotoMode(true);
      const input::Sticks turn = g_car_view.Update(elapsed);
      if (turn.rx != 0) pad.thumb_rx = ToStick(turn.rx);
      if (turn.ry != 0) pad.thumb_ry = ToStick(turn.ry);
      return;
    }
    g_autovista.SetPhotoMode(PhotoModeUp());
    sticks = g_autovista.Update(elapsed);
  }
  if (sticks.lx != 0) pad.thumb_lx = ToStick(sticks.lx);
  if (sticks.ly != 0) pad.thumb_ly = ToStick(sticks.ly);
  if (sticks.rx != 0) pad.thumb_rx = ToStick(sticks.rx);
  if (sticks.ry != 0) pad.thumb_ry = ToStick(sticks.ry);
}

void MainMenuCreated(uint32_t menu) { g_main_menu = menu; }

void LiveryPlacementCreated(uint32_t screen) { g_livery = screen; }

void LiveryColorSelectCreated(uint32_t screen) { g_livery_color = screen; }

void LiveryLayerSelectCreated(uint32_t screen) { g_livery_layers = screen; }

void SplashCreated(uint32_t splash) { g_splash = splash; }

void FocusGroupCreated(uint32_t group) {
  std::lock_guard lock(g_containers_mutex);
  g_groups.insert(group);
}

void IdleScreenCreated(uint32_t screen) {
  std::lock_guard lock(g_containers_mutex);
  g_idle_screens.insert(screen);
}

void IdleScreenDestroyed(uint32_t screen) {
  std::lock_guard lock(g_containers_mutex);
  g_idle_screens.erase(screen);
}

void FocusGroupDestroyed(uint32_t group) {
  std::lock_guard lock(g_containers_mutex);
  g_groups.erase(group);
}

void SetPromptHighlightListener(std::function<void(std::optional<input::UiRect>)> listener) {
  std::lock_guard lock(g_highlight_mutex);
  g_highlight_listener = std::move(listener);
}

// The main menu is up, from the game's own state alone (a screen just made counts at once, before any survey:
// a B still coming from the screen left would otherwise sign the profile out): the screen's bar alive, the
// screen in its menus (not "Press A"), a live list showing the main menu's data source (not FREE PLAY's).
bool MainMenuState() {
  const uint32_t splash = g_splash;
  const uint8_t* const base = g_base;
  if (!base || !GuestPointer(splash)) return false;
  const uint32_t bar = Word(base, splash + kSplashBar), main_source = Word(base, splash + kSplashMainSource);
  if (!GuestPointer(bar) || !GuestPointer(main_source)) return false;
  std::lock_guard lock(g_containers_mutex);
  if (!g_containers.contains(bar) || Word(base, splash) != kSplashVtable || base[splash + kSplashState] != 0) {
    return false;
  }
  for (const uint32_t container : g_containers) {
    if (Word(base, container) == kListVtable && Word(base, container + kDataSource) == main_source) return true;
  }
  return false;
}

void ContainerCreated(uint32_t container) {
  std::lock_guard lock(g_containers_mutex);
  g_containers.insert(container);
}

void ContainerDestroyed(uint32_t container) {
  {
    std::lock_guard lock(g_containers_mutex);
    g_containers.erase(container);
  }
  // Game thread, as Frame: a freed list is not given its focus back.
  std::erase_if(g_unlit, [&](const Unlit& u) { return u.container == container; });
  if (g_hover.container == container) g_hover = {};
  if (g_choose.container == container) g_choose = {};
  if (g_brought.container == container) g_brought = {};
  g_any_unlit = !g_unlit.empty();
}

bool DialogTakesOnlyOk() { return g_dialog_ok_only && input::CurrentInputContext() == input::InputContext::kMenu; }

void GateFocusBack(FocusBackGate& gate, rex::input::X_INPUT_GAMEPAD& pad) {
  const uint16_t pressed = uint16_t(pad.buttons & ~gate.last);
  gate.last = pad.buttons;
  const bool stick = std::abs(int(pad.thumb_lx)) > 16384 || std::abs(int(pad.thumb_ly)) > 16384;
  if (g_any_unlit && !g_modal && (pressed != 0 || stick)) {
    gate.held_back = pad.buttons;
    gate.stick_held_back = stick;
    g_any_unlit = false;
    g_relight = true;
  }
  gate.held_back &= pad.buttons;
  pad.buttons = uint16_t(pad.buttons & ~gate.held_back);
  if (gate.stick_held_back) {
    if (stick) {
      pad.thumb_lx = pad.thumb_ly = 0;
    } else {
      gate.stick_held_back = false;
    }
  }
}

// The scene a control (list or bar) belongs to (0 when unknown).
uint32_t SceneOf(const uint8_t* base, uint32_t container) {
  if (SliderGroup(base, container)) container = Word(base, Word(base, container + kGroupControlsBegin));
  const uint32_t object = GuestPointer(container) ? Word(base, container + kItemObject) : 0;
  return GuestPointer(object) ? Word(base, object + kObjectScene) : 0;
}

void Frame(PPCContext& ctx, uint8_t* base, uint32_t stack) {
  if (!REXCVAR_GET(fm4_menu_mouse) || input::CurrentInputContext() != input::InputContext::kMenu) return;
  g_pages.frame.fetch_add(1, std::memory_order_relaxed);  // pages are looked up afresh each frame
  // A key or pad press, or the wheel (its steps move the focus), gives a put-away focus back first.
  if (g_relight.exchange(false) || (!g_unlit.empty() && input::MenuWheelTaps().Busy())) Relight(ctx, base, stack);
  std::optional<input::UiPoint> pointer;
  bool moved = false, click = false, held = false;
  int right_clicks = 0, notches = 0, sideways_notches = 0;
  {
    Shared& state = State();
    std::lock_guard lock(state.mutex);
    pointer = state.pointer;
    moved = std::exchange(state.moved, false);
    click = std::exchange(state.click, false);
    held = state.held;
    right_clicks = std::exchange(state.right_clicks, 0);
    notches = std::exchange(state.notches, 0);
    sideways_notches = std::exchange(state.sideways_notches, 0);
  }
  PlayPendingChoose(base);
  if (g_brought.container && (!pointer || std::fabs(pointer->x - g_brought.at.x) > kBroughtSlop ||
                              std::fabs(pointer->y - g_brought.at.y) > kBroughtSlop)) {
    g_brought = {};
  }
  if (const uint32_t camera = g_photo_camera; camera && Word(base, camera) != kPhotoCameraVtable) g_photo_camera = 0;
  g_photo_live = g_photo_camera != 0;
  {
    const uint32_t scene = g_race_submenu;
    g_race_photo = GuestPointer(scene) && Word(base, scene) == kRacePhotoVtable;
    const uint32_t splash = g_splash;
    g_title_up = GuestPointer(splash) && Word(base, splash) == kSplashVtable && Byte(base, splash + kSplashState) == 1;
  }
  {
    std::vector<uint32_t> bars, lists;
    {
      std::lock_guard lock(g_containers_mutex);
      for (const uint32_t c : g_containers) {
        if (!GuestPointer(c)) continue;
        const uint32_t vtable = Word(base, c);
        if (vtable == kBarVtable) bars.push_back(c);
        if (vtable == kListVtable) lists.push_back(c);
      }
    }
    UpdateTabKeys(base, lists);
    LevelPromptLabels(ctx, base, stack, bars);
    if (!input::MouseLast()) {
      for (const uint32_t bar : bars) ShowSelectLabel(ctx, base, stack, bar, false);
    }
  }
  // The highlight is the mouse's: a key or the pad takes it away at once.
  if (!input::MouseLast()) NoteHighlight(std::nullopt);
  // The main menu: a right click never goes back there (it would sign the profile out); its prompt takes a click.
  g_base = base;
  if (right_clicks != 0 && MainMenuState()) right_clicks = 0;
  // Kept until the pass below answers it: a frame with a still mouse may not get that far.
  if (right_clicks != 0) {
    g_back_waiting = true;
    g_back_expires_ms = SteadyMs() + kBackWaitMs;
  } else if (g_back_waiting && SteadyMs() >= g_back_expires_ms) {
    g_back_waiting = false;
  }
  double hub_idle = std::numeric_limits<double>::quiet_NaN();
  if (moved || click || right_clicks != 0 || notches != 0 || sideways_notches != 0) {
    hub_idle = KeepAwake(ctx, base, stack);
    WakeIdleScreens(ctx, base, stack);
  }
  if ((right_clicks != 0 || notches != 0 || sideways_notches != 0) && !g_unlit.empty()) Relight(ctx, base, stack);
  // The livery editor placing a layer: the mouse works the layer itself. A drag on the car (above the tabs)
  // follows the tab (MOVE moves, SIZE sizes, SPIN spins...); the wheel sizes, Shift + wheel spins. A click on
  // the car presses nothing (no OK by accident); the tabs and the prompts work as anywhere.
  const auto livery = LiveryUp(ctx, base, stack);
  if (livery) {
    if (notches != 0) LiveryAdjust(ctx, base, stack, *livery, kLiverySize, notches * kLiveryWheelSize,
                                   notches * kLiveryWheelSize);
    if (sideways_notches != 0) LiveryAdjust(ctx, base, stack, *livery, kLiverySpin, sideways_notches * kLiveryWheelSpin, 0);
    notches = sideways_notches = 0;
    // The panel's items are often text points (a tab name's baseline): leave a margin for the text above them.
    if (click && pointer && pointer->y > LiveryPanelTop(base, *livery) + 30) {
      g_livery_drag = LiveryDrag{true, *pointer};
      click = false;
    }
    if (g_livery_drag.active) {
      if (!held || !pointer) {
        g_livery_drag = LiveryDrag{};
      } else if (moved && livery->mode >= 0 && livery->mode < 5) {
        const double dx = pointer->x - g_livery_drag.last.x, dy = pointer->y - g_livery_drag.last.y;
        g_livery_drag.last = *pointer;
        const double gain = kLiveryDragGain[livery->mode];
        if (livery->mode == kLiveryMove || livery->mode == kLiverySize) {
          LiveryAdjust(ctx, base, stack, *livery, livery->mode, dx * gain, dy * gain);
        } else {
          LiveryAdjust(ctx, base, stack, *livery, livery->mode, dx * gain, 0);
        }
      }
      return;
    }
  } else {
    g_livery_drag = LiveryDrag{};
  }
  if (notches != 0 || sideways_notches != 0) g_wheel_led = true;
  if (moved || click) g_wheel_led = false;
  if (notches != 0) input::MenuWheelTaps().AddNotches(notches, g_wheel_along_row);
  if (sideways_notches != 0) input::MenuWheelTaps().AddNotches(sideways_notches, true);
  // A slider being dragged: its value follows the pointer, whole steps at a time, the moment it moves.
  if (g_drag.active) {
    if (!held || !pointer) {
      StopDrag();
    } else if (!input::MenuWheelTaps().Busy()) {
      const auto list = ReadList(base, g_drag.container);
      if (!list || g_drag.item >= int(list->items.size()) || !list->items[size_t(g_drag.item)].slider) {
        StopDrag();
      } else if (list->focus == input::EntryOf(*list, g_drag.item)) {
        const input::MenuItem& item = list->items[size_t(g_drag.item)];
        DragSlider(ctx, base, stack, item, input::SliderTarget(item, pointer->x));
      }
    }
    if (g_drag.active) return;
  }
  // The screen is looked at on every move and click, and a few times a second anyway (a message box that
  // only has OK must be known before Esc is pressed).
  static uint32_t frames = 0;
  const bool survey = ++frames % 6 == 0;
  if (input::MenuWheelTaps().Busy()) NoteHighlight(std::nullopt);  // a press is playing: the screen may change
  if (!moved && !click && !survey && !g_back_waiting && !g_hover_pending) return;
  if (!moved && !click && input::MenuWheelTaps().Busy()) return;
  // While presses are still being played (a wheel spin, a clicked prompt), a move waits; a click waits too
  // (kept for next frame).
  if (input::MenuWheelTaps().Busy()) {
    if (click || moved) {
      Shared& state = State();
      std::lock_guard lock(state.mutex);
      state.click |= click;
      state.moved |= moved;
    }
    return;
  }
  // The focused lists, and the item under the pointer in one of them.
  std::vector<uint32_t> containers;
  {
    std::lock_guard lock(g_containers_mutex);
    // Forget containers whose memory is gone or holds something else now (no vtable from the game's
    // image at +0): freed without the destructor this sees.
    for (auto it = g_containers.begin(); it != g_containers.end();) {
      const uint32_t vtable = Word(base, *it);
      if (vtable < 0x82000000 || vtable >= 0x83000000) {
        if (REXCVAR_GET(fm4_menu_mouse_log)) {
          REXLOG_INFO("Menu mouse: container {:08X} dropped (+0 {:08X}, readable {})", *it, vtable,
                      rex::memory::IsHostReadable(base + (*it & ~0xFFFu)));
        }
        it = g_containers.erase(it);
      } else {
        ++it;
      }
    }
    containers.assign(g_containers.begin(), g_containers.end());
    for (const uint32_t g : g_groups) {
      if (SliderGroup(base, g)) containers.push_back(g);
    }
  }
  // What is on screen: focused lists with shown items, prompt bars with laid-out prompts.
  struct Control {
    uint32_t container = 0;
    std::optional<input::MenuList> list;
    std::vector<input::Prompt> prompts;
    std::optional<HomeButton> home;
  };
  std::vector<Control> controls;
  std::vector<input::Candidate> candidates;
  std::optional<HomeButton> home;
  for (uint32_t container : containers) {
    if (!GuestPointer(container)) continue;
    Control control{container};
    const uint32_t vtable = Word(base, container);
    if (vtable == kListVtable || vtable == kGridVtable || vtable == kGroupVtable) {
      control.list = ReadList(base, container);
      if (!control.list || std::none_of(control.list->items.begin(), control.list->items.end(),
                                        [](const input::MenuItem& item) { return item.shown; })) {
        continue;
      }
    } else if (vtable == kBarVtable) {
      control.prompts = ReadPrompts(base, container);
      if (control.prompts.empty()) continue;
      control.home = ReadHome(base, container);
    } else {
      continue;
    }
    const Layer layer = LayerOf(base, container);
    input::Candidate candidate;
    candidate.list = control.list.has_value();
    candidate.layer = layer.layer;
    candidate.frame = layer.frame;
    candidate.above_bottom_row = std::any_of(control.prompts.begin(), control.prompts.end(),
                                             [](const input::Prompt& p) { return p.y > input::kBottomRowY; });
    if (control.list) {
      bool any = false;
      for (const input::MenuItem& item : control.list->items) {
        if (!item.shown) continue;
        candidate.min_x = any ? std::min(candidate.min_x, item.min_x) : item.min_x;
        candidate.max_x = any ? std::max(candidate.max_x, item.max_x) : item.max_x;
        candidate.min_y = any ? std::min(candidate.min_y, item.min_y) : item.min_y;
        any = true;
      }
    } else {
      input::DialogSpan(control.prompts, candidate.min_x, candidate.max_x, &candidate.min_y);
    }
    controls.push_back(std::move(control));
    candidates.push_back(candidate);
  }
  const input::Active active = input::ChooseActive(candidates);
  {
    bool viewer_list = false;
    for (size_t i = 0; i < controls.size(); ++i) {
      viewer_list |= active.active[i] && controls[i].list && !input::IsTabHeader(*controls[i].list);
    }
    g_viewer_list_up = viewer_list && AutovistaUp();
  }
  // The viewer's UI hidden: its prompts are not there (no highlight, no click); a click brings them back, as Y does.
  // (The flag is off behind the viewer's side menu too: a list on screen means the UI is not what is hidden.)
  const bool list_up = std::any_of(controls.begin(), controls.end(), [](const Control& c) { return c.list.has_value(); });
  if (!list_up && !active.modal && AutovistaUp() && !PhotoModeUp() && ViewerUiHidden(base)) {
    g_sidebar_back_icon = false;
    NoteHighlight(std::nullopt);
    if (click) input::MenuWheelTaps().AddPress(kButtonYBit);
    return;
  }
  // The home button is its bar's: not while a dialog (one holding a list too) has the screen.
  for (size_t i = 0; !home && i < controls.size(); ++i) {
    if (active.active[i]) home = controls[i].home;
  }
  g_modal = active.modal;
  // The wheel moves along a sideways strip when every list on the screen is one (a grid or a column: up / down).
  {
    bool any = false, all_rows = true;
    for (size_t i = 0; i < controls.size(); ++i) {
      if (!active.active[i] || !controls[i].list || input::IsTabHeader(*controls[i].list)) continue;
      any = true;
      all_rows &= input::IsRow(*controls[i].list);
    }
    g_wheel_along_row = any && all_rows;
  }
  // The career hub hid its menu (idle long enough, no list on screen): a move brings it back with one D-pad
  // press, which the hidden menu swallows (a click's A does the same).
  for (const Control& c : controls) {
    if (c.list && !input::IsTabHeader(*c.list)) g_last_list = c.container;
  }
  // A list that comes (back) on screen starts as the game shows it, its focused item lit: an item turned off
  // before it left is lit again (the rules below then turn it off if the pointer is elsewhere).
  {
    static std::vector<uint32_t> on_screen;
    std::vector<uint32_t> now_on;
    for (const Control& c : controls) {
      if (!c.list) continue;
      now_on.push_back(c.container);
      if (std::find(on_screen.begin(), on_screen.end(), c.container) == on_screen.end() && IsUnlit(c.container)) {
        Relight(ctx, base, stack, c.container);
      }
    }
    on_screen = std::move(now_on);
  }
  const bool list_shown = std::any_of(controls.begin(), controls.end(), [](const Control& c) {
    return c.list && !input::IsTabHeader(*c.list);
  });
  // Something under the pointer to click (a woken menu slides back in before its items are in place).
  const bool item_under = pointer && std::any_of(controls.begin(), controls.end(), [&](const Control& c) {
    return c.list && !input::IsTabHeader(*c.list) && input::ItemAt(*c.list, *pointer) >= 0;
  });
  if (g_wake_click != Clock::time_point{}) {
    if (item_under && Clock::now() - g_woke_at > std::chrono::milliseconds(350)) {
      click = true;
      g_wake_click = {};
    } else if (Clock::now() - g_wake_click > std::chrono::milliseconds(1500)) {
      g_wake_click = {};
    }
  }
  // A click while the woken menu is still coming back waits for it too.
  if (click && !item_under && Clock::now() - g_woke_at < std::chrono::milliseconds(1500)) {
    g_wake_click = Clock::now();
    return;
  }
  // Hidden (no list on screen after a few idle seconds), or hiding right now (the hub starts sliding its menu
  // out at about 8 s idle, gone by 9: a press then only wakes it, so a click waits for the menu to come back).
  if ((moved || click) && hub_idle > 5 && (!list_shown || hub_idle > kHubHiding)) {
    // The hub's own screen: the list last seen is in the scene of the hub's bar.
    const auto scene = [&](uint32_t container) {
      const uint32_t object = GuestPointer(container) ? Word(base, container + kItemObject) : 0;
      return GuestPointer(object) ? Word(base, object + kObjectScene) : 0;
    };
    const uint32_t hub_scene = scene(Word(base, g_main_menu + kMainMenuBar));
    if (hub_scene != 0 && scene(g_last_list) == hub_scene) {
      input::MenuWheelTaps().AddSteps(0x0002, 1);
      g_woke_at = Clock::now();
      if (click) g_wake_click = Clock::now();
      if (REXCVAR_GET(fm4_menu_mouse_log)) REXLOG_INFO("Menu mouse: the hub hid its menu ({:.1f} s idle): woken", hub_idle);
      return;
    }
  }
  {
    bool ok = false, back = false;
    for (size_t i = 0; i < controls.size() && active.modal; ++i) {
      if (!active.active[i]) continue;
      for (const input::Prompt& prompt : controls[i].prompts) {
        ok = ok || (prompt.buttons & 0x1000) != 0;
        back = back || (prompt.buttons & 0x2000) != 0;
      }
    }
    if (g_dialog_ok_only.exchange(ok && !back) != (ok && !back) && REXCVAR_GET(fm4_menu_mouse_log)) {
      REXLOG_INFO("Menu mouse: message box with only OK {}", ok && !back ? "up" : "gone");
    }
  }
  // With the mouse the last thing used, what is lit follows the resting pointer too: a screen that opens
  // lights the item under it, or nothing.
  const bool settle = survey && input::MouseLast() && pointer.has_value() && !g_wheel_led;
  if (!moved && !click && !settle && !g_back_waiting && !g_hover_pending) return;
  if ((moved || click) && REXCVAR_GET(fm4_menu_mouse_log)) {
    REXLOG_INFO("Menu mouse: pointer {} {}, {} containers, modal {}", pointer ? pointer->x : 0.0f,
                pointer ? pointer->y : 0.0f, containers.size(), active.modal);
    for (size_t i = 0; i < controls.size(); ++i) {
      const auto& c = controls[i];
      REXLOG_INFO("Menu mouse:   {:08X} {} layer {:02X} frame {:06X} {} {}", c.container, c.list ? "list" : "bar",
                  candidates[i].layer, candidates[i].frame, active.active[i] ? "ACTIVE" : "-",
                  c.list ? fmt::format("focus {} item {} of {}", c.list->focus,
                                       pointer ? input::ItemAt(*c.list, *pointer) : -1, c.list->items.size())
                         : fmt::format("{} prompts", c.prompts.size()));
      if (c.list && c.list->grid_rows == 0 && c.list->items.size() <= 40) {
        std::string rows;
        for (size_t k = 0; k < c.list->items.size(); ++k) {
          const input::MenuItem& it = c.list->items[k];
          if (it.shown) rows += fmt::format(" {}:[{:.0f},{:.0f} {:.0f},{:.0f}]", k, it.min_x, it.min_y, it.max_x, it.max_y);
        }
        REXLOG_INFO("Menu mouse:     items{}", rows);
      }
    }
  }
  // A click on a prompt presses its button (the prompts are drawn over everything); the screen's SELECT is the
  // click on the item (PromptTakesClick), and its label is gone.
  // Whether a click on the bar's own screen chooses an item (a list of its scene, not only values to step or
  // slide: a screen left under an overlay keeps its list on screen); placing a livery layer a left click
  // drags the layer. When none does, OK (A) is the prompt's.
  const auto clicks_choose = [&](uint32_t bar) {
    if (livery) return false;
    const uint32_t bar_scene = SceneOf(base, bar);
    for (size_t i = 0; i < controls.size(); ++i) {
      if (!active.active[i] || !controls[i].list || input::IsTabHeader(*controls[i].list)) continue;
      if (bar_scene != 0 && SceneOf(base, controls[i].container) != bar_scene) continue;
      if (input::ClickChooses(*controls[i].list)) return true;
    }
    return false;
  };
  if (input::MouseLast()) {
    for (const Control& control : controls) {
      if (Word(base, control.container) == kBarVtable) {
        ShowSelectLabel(ctx, base, stack, control.container, clicks_choose(control.container));
      }
    }
  }
  // A right click goes back, also while a screen is still fading in. Not in a dialog with a choice (it would answer
  // it; a box with only OK has nothing to lose, and closes), placing a livery layer, on the viewer's camera or on a
  // page of values whose bar keeps them with A (SAVE, OK: its BACK throws the changes away): those take a click on
  // their own prompt. A page of values with no A on its bar (TUNE SETUP, the pre-race menu's pages) keeps them as
  // they are set, and its BACK is the way out.
  {
    bool values = false;
    for (size_t b = 0; b < controls.size(); ++b) {
      if (!active.active[b] || Word(base, controls[b].container) != kBarVtable) continue;
      const uint32_t bar_scene = SceneOf(base, controls[b].container);
      for (size_t i = 0; i < controls.size(); ++i) {
        if (!active.active[i] || !controls[i].list || input::IsTabHeader(*controls[i].list)) continue;
        if (bar_scene != 0 && SceneOf(base, controls[i].container) != bar_scene) continue;
        values |= input::ValuesOnly(*controls[i].list);
      }
    }
    bool keeps_with_a = false;
    for (size_t i = 0; i < controls.size(); ++i) {
      if (!active.active[i]) continue;
      for (const input::Prompt& prompt : controls[i].prompts) keeps_with_a |= prompt.buttons == kButtonABit;
    }
    const bool camera = AutovistaUp() && !g_viewer_list_up;
    const bool goes_back = DialogTakesOnlyOk() || (!active.modal && !livery && !(values && keeps_with_a) && !camera);
    constexpr float kSidebarRight = -300;  // the left side menu's width: the bottom bars' prompts start past it
    bool back_left = false, back_right = false;
    for (size_t i = 0; i < controls.size(); ++i) {
      if (!active.active[i]) continue;
      for (const input::Prompt& prompt : controls[i].prompts) {
        if (prompt.buttons != kButtonBBit || prompt.y > input::kBottomRowY) continue;
        (prompt.x < kSidebarRight ? back_left : back_right) = true;
      }
    }
    g_sidebar_back_icon = goes_back && !active.modal && !MainMenuState() && back_left && !back_right;
    g_back_waiting = g_back_waiting && goes_back && !MainMenuState();
    if (g_back_waiting && SteadyMs() >= g_back_ready_ms && !input::MenuWheelTaps().Busy()) {
      input::MenuWheelTaps().AddClick(true);
      g_back_waiting = false;
      g_back_ready_ms = SteadyMs() + kBackSpacingMs;
    }
  }
  // The pointer on a prompt a click would press (or the home button): it is highlighted.
  {
    std::optional<input::UiRect> lit;
    // Not while a click plays out: the line must not stay on what is about to move.
    const bool mouse = pointer.has_value() && input::MouseLast() && !g_choose.container;
    if (mouse && home && !active.modal && home->Hit(*pointer)) {
      lit = input::UiRect{home->min_x, home->min_y, home->max_x, home->max_y};
    }
    for (size_t i = 0; mouse && !lit && i < controls.size(); ++i) {
      if (!active.active[i]) continue;
      for (const input::Prompt& prompt : controls[i].prompts) {
        if (!input::PromptHit(prompt, *pointer)) continue;
        if (input::PromptTakesClick(prompt, clicks_choose(controls[i].container))) {
          input::Prompt text = prompt;
          text.icon_hidden = true;  // under the label alone
          lit = input::PromptBox(text);
        }
        break;
      }
    }
    // A carousel's side tile (car or paint tiles): the game's highlight is the slot's, and moving it would scroll
    // the strip under the pointer, so the tile is lit from above; a click brings it in, the next one opens it.
    for (size_t i = 0; mouse && !lit && !active.modal && !g_brought.container && i < controls.size(); ++i) {
      if (!active.active[i] || !controls[i].list || input::IsTabHeader(*controls[i].list)) continue;
      const input::MenuList& list = *controls[i].list;
      if (list.grid_rows > 0) continue;
      const int item = input::ItemAt(list, *pointer);
      if (item < 0 || !FocusScrolls(base, controls[i].container, list, item)) continue;
      const input::MenuItem& tile = list.items[size_t(item)];
      if (!tile.id || !input::HorizontalList(list)) continue;
      lit = input::UiRect{tile.min_x, tile.min_y, tile.max_x, tile.max_y, true};
    }
    for (size_t i = 0; mouse && !lit && !active.modal && i < controls.size(); ++i) {
      if (!active.active[i] || !controls[i].list || controls[i].list->grid_rows <= 0) continue;
      const input::MenuList& list = *controls[i].list;
      const int cell = input::ItemAt(list, *pointer);
      if (cell < 0 || input::EntryOf(list, cell) == list.focus) continue;
      const input::MenuItem& c = list.items[size_t(cell)];
      lit = input::UiRect{c.min_x, c.min_y, c.max_x, c.max_y, true};
    }
    NoteHighlight(lit);
  }
  // The home button: a click on it is the pad's BACK (Tab), as the game shows it. Not under a dialog.
  if (click && pointer && home && !active.modal && home->Hit(*pointer)) {
    if (!g_unlit.empty()) Relight(ctx, base, stack);
    input::MenuWheelTaps().AddPress(kButtonBackBit);
    return;
  }
  if (click && pointer) {
    for (size_t i = 0; i < controls.size(); ++i) {
      if (!active.active[i]) continue;
      for (const input::Prompt& prompt : controls[i].prompts) {
        if (input::PromptHit(prompt, *pointer)) {
          if (!input::PromptTakesClick(prompt, clicks_choose(controls[i].container))) return;
          if (!g_unlit.empty()) Relight(ctx, base, stack);
          input::MenuWheelTaps().AddPress(prompt.buttons);
          return;
        }
      }
    }
  }
  // While a dialog is up only its prompts take the mouse.
  if (active.modal) return;
  // A click on a tab header's neighbouring name (or its Q / E arrow) changes tab: LB / RB.
  if (click && pointer) {
    for (size_t i = 0; i < controls.size(); ++i) {
      if (!active.active[i] || !controls[i].list) continue;
      const int tab = input::TabClick(*controls[i].list, *pointer);
      if (tab != 0) {
        if (!g_unlit.empty()) Relight(ctx, base, stack);
        input::MenuWheelTaps().AddPress(tab < 0 ? 0x100 : 0x200);
        return;
      }
    }
  }
  // The second click on a tile just brought in: it opens, whatever has slid under the pointer.
  if (click && g_brought.container) {
    const BroughtIn brought = std::exchange(g_brought, BroughtIn{});
    const auto list = ReadList(base, brought.container);
    if (list && list->focus == brought.entry && NowSeconds() - brought.when >= kStepClicks) {
      if (!g_unlit.empty()) Relight(ctx, base, stack);
      input::MenuWheelTaps().AddClick(false);
      return;
    }
  }
  bool any_list = false;
  bool on_item = false;
  for (size_t i = 0; i < controls.size(); ++i) {
    if (!active.active[i] || !controls[i].list) continue;
    const input::MenuList& list = *controls[i].list;
    if (input::IsTabHeader(list)) continue;  // tabs change by click only (LB / RB), never by D-pad
    any_list = true;
    const int item = pointer ? input::ItemAt(list, *pointer) : -1;
    if (item < 0) continue;
    on_item = true;
    // The item under the pointer lights up (takes the focus) at once; a click then works it: a slider's bar
    // drags, a spinner steps (its left arrow back), anything else is chosen (A). A click the game will not
    // focus (a screen changing under it) presses nothing.
    const input::MenuItem& hit = list.items[size_t(item)];
    // A strip's side item (a carousel's, a half-shown row's) lights for the look only: the focus stays.
    if (!click) {
      if (list.grid_rows > 0) {
        EndHover(ctx, base, stack);  // a grid's cell is lit from above (the highlight): the focus stays
      } else if (!FocusScrolls(base, controls[i].container, list, item)) {
        EndHover(ctx, base, stack);
        // At most one focus move each kHoverGap: a fast sweep would fire the focus sound for every row it crosses.
        if (NowSeconds() - g_hover_moved < kHoverGap) {
          g_hover_pending = true;
          return;
        }
        g_hover_pending = false;
        g_hover_moved = NowSeconds();
        FocusItem(ctx, base, stack, controls[i].container, list, item);
      } else {
        HoverItem(ctx, base, stack, controls[i].container, list, item);
      }
      return;
    }
    // A click on a carousel's side tile brings it in and stops there (the next click opens it); a half-shown row
    // is brought in and chosen once it is there; a value to step or slide is only brought in.
    // A grid's cell: the first click picks it (the game shows it: a colour on the car), a click on the one picked
    // chooses it, as A does.
    if (list.grid_rows > 0 && input::EntryOf(list, item) != list.focus) {
      FocusItem(ctx, base, stack, controls[i].container, list, item);
      NoteHighlight(std::nullopt);
      return;
    }
    const bool brings_in = FocusScrolls(base, controls[i].container, list, item);
    if (brings_in) EndHover(ctx, base, stack);
    const bool there = FocusItem(ctx, base, stack, controls[i].container, list, item);
    if (brings_in) {
      if (input::HorizontalList(list)) {
        if (there) g_brought = {controls[i].container, input::EntryOf(list, item), *pointer, NowSeconds()};
        NoteHighlight(std::nullopt);
      } else if (!hit.slider && !hit.spinner) {
        g_choose = {controls[i].container, input::EntryOf(list, item), NowSeconds(), 0};
      }
      return;
    }
    if (!there) return;
    if (input::OnSliderBar(hit, *pointer)) {
      StopDrag();
      g_drag.active = true, g_drag.container = controls[i].container, g_drag.item = item;
    } else if (hit.spinner) {
      // A full tap (some screens miss a one-frame press: ASSISTS & DIFFICULTY).
      const bool back = input::SpinnerClick(hit, pointer->x) < 0;
      input::MenuWheelTaps().AddPress(back ? 0x0004 : 0x0008);
    } else if (!hit.slider && Word(base, hit.id) != kSliderVtable) {
      input::MenuWheelTaps().AddClick(false);
    }
    return;
  }
  // The pointer on no item: nothing is lit.
  if (!click && !on_item) {
    EndHover(ctx, base, stack);
    for (size_t i = 0; i < controls.size(); ++i) {
      if (!active.active[i] || !controls[i].list || input::IsTabHeader(*controls[i].list)) continue;
      Unlight(ctx, base, stack, controls[i].container, *controls[i].list);
    }
  }
  // Nothing to click on the screen (no list, no A prompt: "Press A to start the game"): a click is A, as a
  // pad's would be. With a prompt to choose with, a click off it does nothing. Not while a livery layer is
  // placed: there OK is the prompt's.
  // The Autovista viewer: a click (a press let go without walking) on the focused part's icon opens it (A);
  // anywhere else in the picture it does nothing.
  if (click && !any_list && pointer && pointer->y > input::kBottomRowY && AutovistaUp()) {
    if (!PhotoModeUp() && OnAutovistaIcon(base, *pointer)) input::MenuWheelTaps().AddClick(false);
    return;
  }
  bool a_prompt = false;
  for (size_t i = 0; i < controls.size(); ++i) {
    if (!active.active[i]) continue;
    for (const input::Prompt& prompt : controls[i].prompts) a_prompt |= (prompt.buttons & kButtonABit) != 0;
  }
  if (click && !any_list && !a_prompt && !livery) {
    if (!g_unlit.empty()) Relight(ctx, base, stack);
    input::MenuWheelTaps().AddClick(false);
  }
}

}  // namespace fm4_static_recomp::menu_mouse

// CUIContainerCtrl's constructor, after it set its vtables (r31 = the container).
void Fm4UiContainerCreated(PPCRegister& r31) {
  fm4_static_recomp::menu_mouse::ContainerCreated(r31.u32);
}

// CSplash's constructor, at its end (r31 = the main menu).
void Fm4SplashCreated(PPCRegister& r31) {
  fm4_static_recomp::menu_mouse::SplashCreated(r31.u32);
}

// CLiveryLayerPosition's constructor, at its end (r31 = the placement screen).

// The UI photo mode camera's update (r3 = the camera).
void Fm4PhotoModeCameraUpdate(PPCRegister& r3) { fm4_static_recomp::menu_mouse::PhotoModeStarted(r3.u32); }
void Fm4IdleScreenCreated(PPCRegister& r31) { fm4_static_recomp::menu_mouse::IdleScreenCreated(r31.u32); }
void Fm4IdleScreenCreated2(PPCRegister& r31) { fm4_static_recomp::menu_mouse::IdleScreenCreated(r31.u32); }
void Fm4IdleScreenDestroyed(PPCRegister& r31) { fm4_static_recomp::menu_mouse::IdleScreenDestroyed(r31.u32); }
void Fm4IdleScreenDestroyed2(PPCRegister& r31) { fm4_static_recomp::menu_mouse::IdleScreenDestroyed(r31.u32); }

void Fm4FocusGroupCreated(PPCRegister& r3) { fm4_static_recomp::menu_mouse::FocusGroupCreated(r3.u32); }

void Fm4FocusGroupDestroyed(PPCRegister& r3) { fm4_static_recomp::menu_mouse::FocusGroupDestroyed(r3.u32); }

void Fm4LiveryPlacementCreated(PPCRegister& r31) {
  fm4_static_recomp::menu_mouse::LiveryPlacementCreated(r31.u32);
}

void Fm4LiveryColorSelectCreated(PPCRegister& r31) {
  fm4_static_recomp::menu_mouse::LiveryColorSelectCreated(r31.u32);
}

void Fm4LiveryLayerSelectCreated(PPCRegister& r31) {
  fm4_static_recomp::menu_mouse::LiveryLayerSelectCreated(r31.u32);
}

// CMainMenu's constructor, at its end (r31 = the career hub).
void Fm4MainMenuCreated(PPCRegister& r31) {
  fm4_static_recomp::menu_mouse::MainMenuCreated(r31.u32);
}

// CUIContainerCtrl's destructor, as it starts (r3 = the container).
void Fm4UiContainerDestroyed(PPCRegister& r3) {
  fm4_static_recomp::menu_mouse::ContainerDestroyed(r3.u32);
}

// The Natal cursor's update, once a UI frame on the game thread (r1: its caller's stack).
void Fm4UiCursorFrame(PPCContext& ctx, uint8_t* base, PPCRegister& r1) {
  fm4_static_recomp::menu_mouse::Frame(ctx, base, r1.u32);
}
