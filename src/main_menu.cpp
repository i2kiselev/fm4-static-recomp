// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "main_menu.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <utility>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "game_hooks.h"
#include "guest_addresses.h"

REXCVAR_DEFINE_BOOL(fm4_skip_title_screen, true, "Forza Motorsport 4",
                    "Goes past the title screen (\"Press A to start the game\") on its own, and the main "
                    "menu's Back asks to exit to the desktop; false keeps the game's own front end");

namespace fm4_static_recomp::main_menu {
namespace {

// The screen (CSplash). +0x239: 1 "Press A", 0 the menus. Its menu object (+0x160) shows a list (+0x140) of a data
// source (+0x70): the main menu's own (the screen's +0x164), or FREE PLAY's while that menu is open.
using fm4_guest::kSplashVtable;
constexpr uint32_t kState = 0x239, kMenu = 0x160, kMainSource = 0x164, kBar = 0x168;
constexpr uint32_t kMenuList = 0x140, kListSource = 0x70;
// Once the screen has taken A it is going to the main menu ("going to another screen", as the race menus' +272).
constexpr uint32_t kNavigating = 272;
constexpr double kTitleSettle = 0.5;  // seconds after the screen is made before the first A

// The bar's prompts: data source +0x13C, entries [+0x18, +0x1C) of 0x38 bytes, the button at +0x08. The game's own
// replace takes the entry out (kSourceRemove(source, index)) and puts the new one in its place (kSourceInsert(source,
// index, button, label, icon path, button, gap f1, 1)); the bar then lays out with virtual +0x28 of its view (+0x6C).
// The icon path is the prompt style's (+0x158, 11 per style in kPromptIcons) or the pad's BACK button, which no style
// has. A label: kMakeLocString(out, table, key), then kMakeMenuLabel(label, string).
constexpr uint32_t kBarSource = 0x13C, kSourceBegin = 0x18, kSourceEnd = 0x1C, kSourceStride = 0x38, kSourceButton = 0x08;
constexpr uint32_t kSourceRemove = fm4_guest::kPromptSourceRemove, kSourceInsert = fm4_guest::kPromptSourceInsert;
using fm4_guest::kPromptGap;
using fm4_guest::kPromptIcons;
// The bar's view is embedded at +0x6C (its own vtable there): the layout is called on that address.
constexpr uint32_t kBarStyle = 0x158, kBarView = 0x6C, kViewLayout = 0x28;
using fm4_guest::kMakeLocString;
using fm4_guest::kMakeMenuLabel;
using fm4_guest::kHelpButtonsTable;
using fm4_guest::kBackStringKey;  // IDS_Back's key, set by the game at start
constexpr uint32_t kPromptButtonB = 1;
// The box: kShowYesNo(screen, message, title, on YES, on NO, sound, 0), as the screens ask (YES on A, NO on B). A
// button's callback is kMakeScreenCallback(holder, {screen, id}) (a 0x20-byte holder, its reference at +0x1C):
// pressed, it calls 0x824E48B8(screen, id), which ScreenAnswer takes for our two ids.
// kMakeScreenCallback copies the {screen, id} pair into its own object, so the pair can live in stack scratch.
using fm4_guest::kShowYesNo;
using fm4_guest::kMakeScreenCallback;
using fm4_guest::kYesNoSound;
// A screen's popup over it (CPopupMsgBox: the game's boxes and ours), 0 when there is none.
constexpr uint32_t kScreenPopup = 0xB4;
constexpr uint32_t kCallbackHolder = 0x20, kHolderReference = 0x1C;
constexpr uint32_t kExitYes = 0x7E570001, kExitNo = 0x7E570002;

std::atomic<uint32_t> g_splash{0};        // the screen while it lives
std::atomic<uint32_t> g_title_splash{0};  // the session's first, while A is still to be pressed on it
std::atomic<double> g_title_made{0};
std::atomic<bool> g_title_seen{false};
std::atomic<bool> g_asked{false};     // Back pressed: the box, next frame
std::atomic<bool> g_box_open{false};  // from the box's showing to its answer
double g_box_shown = 0;  // frame thread: when ours was shown (it reaches the screen's popup slot a little later)
std::atomic<bool> g_pad_prompts{false};
bool g_pad_back_down = false;  // pad thread
// Frame thread: what the bar's BACK slot says now.
uint32_t g_labelled_bar = 0;
bool g_label_exit = false, g_label_pad = false;
std::mutex g_quit_mutex;
std::function<void()> g_quit;

double Clock() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

uint8_t* Base() { return rex::system::kernel_state()->memory()->virtual_membase(); }

bool GuestPointer(uint32_t p) { return p >= 0x10000 && p < 0x9F000000 && (p & 3) == 0; }

uint32_t Word(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}

void StoreWord(uint8_t* base, uint32_t a, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(base + a, &value, 4);
}

template <typename Setup>
void CallGuest(PPCContext& ctx, uint8_t* base, uint32_t stack, uint32_t address, Setup setup) {
  PPCFunc* const function = rex::runtime::ResolveIndirectFunction(address);
  if (!function) return;
  const uint32_t saved_csr = ctx.fpscr.csr;
  PPCContext nested = ctx;
  nested.dispatch_address = 0;
  nested.lr = 0;
  nested.r1.u32 = stack - 0x100;
  setup(nested);
  function(nested, base);
  ctx.fpscr.csr = saved_csr;
  ctx.fpscr.setcsr(saved_csr);
}

void CallVirtual(PPCContext& ctx, uint8_t* base, uint32_t stack, uint32_t object, uint32_t offset) {
  if (!GuestPointer(object)) return;
  const uint32_t vtable = Word(base, object);
  const uint32_t function = vtable >= 0x82000000 && vtable < 0x83000000 ? Word(base, vtable + offset) : 0;
  if (function < 0x82000000 || function >= 0x83000000) return;
  CallGuest(ctx, base, stack, function, [&](PPCContext& c) {
    c.r3.u64 = object;
    c.r4.u64 = 0;
  });
}

// A label from the prompts' string table into the two words at `holders` (its string, the label).
void MakeLabel(PPCContext& ctx, uint8_t* base, uint32_t stack, uint32_t holders, uint32_t key) {
  std::memset(base + holders, 0, 8);
  CallGuest(ctx, base, stack, kMakeLocString, [&](PPCContext& c) {
    c.r3.u64 = holders;
    c.r4.u64 = kHelpButtonsTable;
    c.r5.u64 = key;
  });
  CallGuest(ctx, base, stack, kMakeMenuLabel, [&](PPCContext& c) {
    c.r3.u64 = holders + 4;
    c.r4.u64 = holders;
  });
}

// Lets go of the references in these words (labels, strings, callbacks), as the game's screens do.
void Release(PPCContext& ctx, uint8_t* base, uint32_t stack, std::initializer_list<uint32_t> words) {
  for (const uint32_t word : words) CallVirtual(ctx, base, stack, Word(base, word), 8);
}

// The pad's BACK button icon for a prompt style (the game ships it beside the others; no table lists it).
uint32_t BackIconPath(uint32_t style) {
  static uint32_t paths[2] = {};
  const uint32_t which = style == 1 ? 1 : 0;
  if (!paths[which]) {
    const char* const text = which ? "GAME:\\Media\\UI\\Textures\\UI4\\Back_Button_D_F3.tga"
                                   : "GAME:\\Media\\UI\\Textures\\UI4\\Back_Button_F3.tga";
    const uint32_t size = uint32_t(std::strlen(text) + 1);
    auto* const memory = rex::system::kernel_state()->memory();
    const uint32_t address = memory->SystemHeapAlloc(size);
    std::memcpy(memory->virtual_membase() + address, text, size);
    paths[which] = address;
  }
  return paths[which];
}

// The main menu's own menu, with nothing over it (no box: the game's own take B and BACK themselves).
bool TopLevelOf(const uint8_t* base, uint32_t splash) {
  if (!GuestPointer(splash) || Word(base, splash) != kSplashVtable || base[splash + kState] != 0) return false;
  if (Word(base, splash + kScreenPopup) != 0) return false;
  const uint32_t menu = Word(base, splash + kMenu), main = Word(base, splash + kMainSource);
  const uint32_t list = GuestPointer(menu) ? Word(base, menu + kMenuList) : 0;
  return GuestPointer(main) && GuestPointer(list) && Word(base, list + kListSource) == main;
}

// The bar's BACK slot says EXIT on the main menu's own menu (as short as BACK: the bar lays its prompts out
// from a fixed start, a longer label would run past its end) (its icon the pad's BACK button while a pad
// was used last) and the game's BACK on FREE PLAY's. Scratch goes below the caller's stack, the calls below that.
// Whether the bar has laid its prompts out (each label measured): only then is the label changed, so the bar lays
// the new one out from its real width, as it does its own.
void SetBackPrompt(PPCContext& ctx, uint8_t* base, uint32_t caller_stack, uint32_t bar, bool exit) {
  const bool pad = exit && g_pad_prompts;
  if (bar == g_labelled_bar && exit == g_label_exit && pad == g_label_pad) return;
  const uint32_t source = Word(base, bar + kBarSource), style = Word(base, bar + kBarStyle);
  if (!GuestPointer(source) || style > 2) return;
  int index = -1;
  const uint32_t first = Word(base, source + kSourceBegin), last = Word(base, source + kSourceEnd);
  for (uint32_t entry = first, i = 0; GuestPointer(first) && entry + kSourceStride <= last && i < 16;
       entry += kSourceStride, ++i) {
    if (Word(base, entry + kSourceButton) == kPromptButtonB) {
      index = int(i);
      break;
    }
  }
  if (index < 0) return;  // the bar is not filled yet
  g_labelled_bar = bar, g_label_exit = exit, g_label_pad = pad;
  const uint32_t stack = caller_stack - 0x140, holders = stack;  // scratch [stack, stack + 8)
  MakeLabel(ctx, base, stack, holders,
            exit ? uint32_t(kExitToDesktopStringKey) : Word(base, kBackStringKey));
  const uint32_t icon = pad ? BackIconPath(style) : Word(base, kPromptIcons + 4 * (style * 11 + kPromptButtonB));
  const uint32_t gap_bits = Word(base, kPromptGap);
  float gap;
  std::memcpy(&gap, &gap_bits, 4);
  CallGuest(ctx, base, stack, kSourceRemove, [&](PPCContext& c) {
    c.r3.u64 = source;
    c.r4.u64 = uint32_t(index);
  });
  CallGuest(ctx, base, stack, kSourceInsert, [&](PPCContext& c) {
    c.r3.u64 = source;
    c.r4.u64 = uint32_t(index);
    c.r5.u64 = kPromptButtonB;
    c.r6.u64 = holders + 4;
    c.r7.u64 = icon;
    c.r8.u64 = kPromptButtonB;
    c.f1.f64 = gap;
    c.r10.u64 = 1;
  });
  Release(ctx, base, stack, {holders + 4, holders});
  CallVirtual(ctx, base, stack, bar + kBarView, kViewLayout);
}

// The game's YES / NO box over the main menu: EXIT TO DESKTOP and its question.
void ShowBox(PPCContext& ctx, uint8_t* base, uint32_t caller_stack, uint32_t splash) {
  const uint32_t stack = caller_stack - 0x1C0, s = stack;  // scratch [s, s + 0x60)
  std::memset(base + s, 0, 0x60);
  const uint32_t yes = s, no = s + kCallbackHolder, yes_arg = s + 0x40, no_arg = s + 0x48;
  const uint32_t title = s + 0x50, message = s + 0x58;  // each: string, label
  StoreWord(base, yes_arg, splash);
  StoreWord(base, yes_arg + 4, kExitYes);
  StoreWord(base, no_arg, splash);
  StoreWord(base, no_arg + 4, kExitNo);
  MakeLabel(ctx, base, stack, title, kExitToDesktopStringKey);
  MakeLabel(ctx, base, stack, message, kExitToDesktopQuestionKey);
  for (const auto& [holder, arg] : {std::pair{yes, yes_arg}, std::pair{no, no_arg}}) {
    CallGuest(ctx, base, stack, kMakeScreenCallback, [&](PPCContext& c) {
      c.r3.u64 = holder;
      c.r4.u64 = arg;
    });
  }
  CallGuest(ctx, base, stack, kShowYesNo, [&](PPCContext& c) {
    c.r3.u64 = splash;
    c.r4.u64 = message + 4;
    c.r5.u64 = title + 4;
    c.r6.u64 = yes;
    c.r7.u64 = no;
    c.r8.u64 = Word(base, kYesNoSound);
    c.r9.u64 = 0;
  });
  Release(ctx, base, stack, {title + 4, title, message + 4, message, yes + kHolderReference, no + kHolderReference});
  REXLOG_INFO("Main menu: EXIT TO DESKTOP box");
}

}  // namespace

bool Enabled() { return REXCVAR_GET(fm4_skip_title_screen); }

void SplashCreated(uint32_t splash) {
  if (!splash) return;
  g_splash = splash;
  g_labelled_bar = 0;
  REXLOG_INFO("Title screen created {:08X}", splash);
  if (g_title_seen.exchange(true)) return;  // A only on the session's first
  g_title_made = Clock();
  g_title_splash = splash;
}

void SplashDestroyed(uint32_t splash) {
  uint32_t expected = splash;
  g_title_splash.compare_exchange_strong(expected, 0);
  expected = splash;
  if (splash && g_splash.compare_exchange_strong(expected, 0)) {
    g_labelled_bar = 0;
    g_box_open = false;
    g_asked = false;
    REXLOG_INFO("Title screen destroyed {:08X}", splash);
  }
}

bool TitleScreenWantsA() {
  const uint32_t splash = g_title_splash;
  if (!Enabled() || !splash) return false;
  const uint8_t* const base = Base();
  if (base[splash + kNavigating] || (base[splash + kState] == 0 && TopLevelOf(base, splash))) {
    uint32_t expected = splash;
    if (g_title_splash.compare_exchange_strong(expected, 0)) {
      REXLOG_INFO("Title screen {:08X} took A: going to the main menu", splash);
    }
    return false;
  }
  // A press every 0.6 s once the screen has settled (one too early is ignored), held 0.15 s; never past 20 s.
  const double t = Clock() - g_title_made - kTitleSettle;
  return t >= 0 && t < 20.0 && std::fmod(t, 0.6) < 0.15;
}

bool TopLevel() { return Enabled() && TopLevelOf(Base(), g_splash); }

void FilterPad(rex::input::X_INPUT_GAMEPAD& pad) {
  if (TitleScreenWantsA()) pad.buttons = uint16_t(pad.buttons | rex::input::X_INPUT_GAMEPAD_A);
  const bool back = (pad.buttons & rex::input::X_INPUT_GAMEPAD_BACK) != 0;
  const bool pressed = back && !g_pad_back_down;
  g_pad_back_down = back;
  if (g_box_open || !TopLevel()) return;  // the box takes B as NO
  pad.buttons = uint16_t(pad.buttons & ~rex::input::X_INPUT_GAMEPAD_B);
  if (pressed) g_asked = true;
}

bool BackPressed(uint32_t splash) {
  if (!Enabled() || splash != g_splash || !TopLevelOf(Base(), splash)) return false;
  g_asked = true;
  return true;
}

void Frame(PPCContext& ctx, uint8_t* base, uint32_t stack) {
  const uint32_t splash = g_splash;
  if (!Enabled() || !GuestPointer(splash) || Word(base, splash) != kSplashVtable) return;
  const uint32_t bar = Word(base, splash + kBar);
  if (!GuestPointer(bar)) {
    g_labelled_bar = 0;  // set again once the bar is back (the game may have rebuilt it)
    return;
  }
  // The label goes in as soon as the bar has its BACK, before the bar shows: the bar sizes its prompts when it
  // comes on screen and does not size them again, so a label put in later would run past the bar's end.
  const bool menus = base[splash + kState] == 0;
  const bool top = !menus || TopLevelOf(base, splash);
  SetBackPrompt(ctx, base, stack, bar, top);
  if (!menus) return;
  if (Word(base, splash + kScreenPopup) == 0 && Clock() - g_box_shown > 0.5) g_box_open = false;
  if (g_asked.exchange(false) && top && !g_box_open) {
    g_box_open = true;
    g_box_shown = Clock();
    ShowBox(ctx, base, stack, splash);
  }
}

bool ScreenAnswer(uint32_t screen, uint32_t id) {
  if ((id != kExitYes && id != kExitNo) || screen != g_splash) return false;
  g_box_open = false;
  if (id == kExitYes) {
    REXLOG_INFO("Main menu: EXIT TO DESKTOP: YES");
    std::function<void()> quit;
    {
      std::lock_guard lock(g_quit_mutex);
      quit = g_quit;
    }
    if (quit) quit();
  }
  return true;
}

void SetQuit(std::function<void()> quit) {
  std::lock_guard lock(g_quit_mutex);
  g_quit = std::move(quit);
}

void SetPadPrompts(bool pad) { g_pad_prompts = pad; }

}  // namespace fm4_static_recomp::main_menu
