// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include <rex/input/input.h>
#include <rex/ppc/context.h>

#include "input/menu_hit.h"

// The mouse in the game's menus (#76): the item under the pointer lights up (takes the focus), a click
// chooses it, and with the pointer off every item nothing is lit. The window thread records the pointer
// (in the game's UI space) and clicks; the game thread, once a UI frame, finds the active menu list among
// the live UI containers, hit-tests its shown items and moves the focus with the game's own routines.
namespace fm4_static_recomp::menu_mouse {

// From the window thread.
void PointerMoved(std::optional<input::UiPoint> point);  // nullopt: off the game's picture
void LeftClick();
void LeftRelease();
// Right click (back, B) and wheel notches (D-pad steps): played from the game thread, after a focus the
// pointer turned off is lit again (a list whose focused item is not lit ignores the pad).
void RightClick();
void WheelNotches(int notches, bool sideways);
void RightRelease();
// Leaving the menus (a race): forget the pointer and any click.
void Reset();

// The Autovista viewer (input/autovista_mouse.h): a left drag walks its camera around the car, the wheel
// steps closer or back and a right drag turns the head; a press let go without moving is the click it
// was (A, or B on the right). The prompts at the bottom take clicks as anywhere.
// From the game thread, every tick while the viewer is open (hook Fm4AutovistaCameraUpdate).
void AutovistaTick(uint32_t camera);
// From the game thread, with the UI photo mode's (TAKE PHOTO) camera: worked the same way while that camera lives,
// its right drag turning the view at the pointer's speed (its look stays where it is left).
void PhotoModeStarted(uint32_t camera);
// From the game thread, as a race's submenu scene (photo mode among them) is created and destroyed.
void RaceSubmenuCreated(uint32_t scene);
void RaceSubmenuDestroyed(uint32_t scene);
// The title screen's "Press A" is up.
bool TitleScreenUp();
// The screen's BACK is in a left side menu, and a right click does it: it shows the mouse's right button there.
// (The bottom bars stay plain text.)
bool SidebarBackIcon();
// From the keyboard filter: the sticks the mouse works while the viewer is open (a stick at rest is left
// to the keys).
void AutovistaSticks(rex::input::X_INPUT_GAMEPAD& pad, double elapsed);

// From the keyboard and pad filters (each with its own `gate`): while the mouse has put a list's focus
// away (the pointer off its items), the first button press or stick push brings it back where it was and
// does nothing else; it is held back from the game until let go.
struct FocusBackGate {
  uint16_t last = 0;
  uint16_t held_back = 0;
  bool stick_held_back = false;
};
void GateFocusBack(FocusBackGate& gate, rex::input::X_INPUT_GAMEPAD& pad);

// A message box that only has OK (FEATURE UNAVAILABLE...) is up: the keyboard's and the mouse's back (Esc,
// right click) close it with OK, as on any PC; a pad's B is left as the game has it.
bool DialogTakesOnlyOk();
// The OK-only box has been answered: the next press is for the screen under it.
void DialogAnswered();

// From the game thread, once a UI frame (the Natal cursor's update); `stack` is free guest stack below
// which game routines can be called (a slider's step).
void Frame(PPCContext& ctx, uint8_t* base, uint32_t stack);

// The career hub (CMainMenu), as the game makes it: its menu hides after a while with no input.
void MainMenuCreated(uint32_t menu);

// The main menu (CSplash), as the game makes it: a right click never goes back there (it would sign the profile out).
void SplashCreated(uint32_t splash);
// A screen's focus group made / gone (hooks Fm4FocusGroupCreated / Fm4FocusGroupDestroyed).
void FocusGroupCreated(uint32_t group);
// A screen that hides its menu when idle (pre-race menu, car colour choice) made / gone.
void IdleScreenCreated(uint32_t screen);
void IdleScreenDestroyed(uint32_t screen);
void FocusGroupDestroyed(uint32_t group);
// The prompt (or the home button) under the pointer that a click would press, for the pointer's highlight
// (UI space); nullopt when there is none. Called when it changes, from the game thread or a pad filter.
void SetPromptHighlightListener(std::function<void(std::optional<input::UiRect>)> listener);

// The livery editor's layer placement screen (CLiveryLayerPosition: MOVE / SIZE / SPIN / SKEW / TRANSPARENCY),
// as the game makes it: there the mouse drags the layer, the wheel sizes it and Shift + wheel spins it.
void LiveryPlacementCreated(uint32_t screen);
void LiveryColorSelectCreated(uint32_t screen);
void LiveryLayerSelectCreated(uint32_t screen);

// UI containers as the game makes and frees them (CUIContainerCtrl).
void ContainerCreated(uint32_t container);
void ContainerDestroyed(uint32_t container);

}  // namespace fm4_static_recomp::menu_mouse
