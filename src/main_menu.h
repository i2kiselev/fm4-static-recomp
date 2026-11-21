// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <functional>

#include <rex/input/input.h>
#include <rex/ppc/context.h>

// The game's front end as a PC game has it (#126): the title screen ("Press A to start the game") is passed on its
// own, the main menu is the first screen, and its Back is EXIT TO DESKTOP, asked in the game's own YES / NO box.
// fm4_skip_title_screen (on by default) turns all of it on; off, the front end is the game's own.
namespace fm4_static_recomp::main_menu {

bool Enabled();

// The title screen / main menu (CSplash: one screen, +0x239 1 "Press A", 0 the menus) as it is made and freed.
void SplashCreated(uint32_t splash);
void SplashDestroyed(uint32_t splash);

// While the session's first title screen is up: A should be pressed on the first pad (pulsed, as a press).
bool TitleScreenWantsA();

// The main menu's own menu is up (not "Press A", not FREE PLAY's menu, which is the same screen), from any thread.
bool TopLevel();

// The first pad's state each poll: a press of A on the title screen; on the main menu B is kept (it would sign the
// profile out, and is too easy to press by mistake) and BACK asks to exit.
void FilterPad(rex::input::X_INPUT_GAMEPAD& pad);

// The main menu's Back, about to sign out and go back to "Press A" (r30 = the screen): true to ask instead.
bool BackPressed(uint32_t splash);

// Each frame of a screen's update (game thread, `stack` the caller's r1): on the main menu, BACK's label and the box.
void Frame(PPCContext& ctx, uint8_t* base, uint32_t screen, uint32_t stack);

// A popup button's answer (0x824E48B8(screen, id)): true when it was the box's, which the screen never sees.
bool ScreenAnswer(uint32_t screen, uint32_t id);

// What YES does (closes the window).
void SetQuit(std::function<void()> quit);
// A pad was used last: the main menu's prompt shows the pad's BACK button (the keyboard's shows Esc).
void SetPadPrompts(bool pad);

}  // namespace fm4_static_recomp::main_menu
