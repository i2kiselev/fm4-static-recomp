# Keyboard

[← Back to the README](../README.md)

The keyboard plays beside a gamepad, with a layout after Forza Horizon 6's. While the
keyboard is the last device you used, the game's button prompts show keycaps of the bound
keys, and English prompt sentences ("PRESS THE BACK BUTTON...") name them too; touching the
pad brings the controller buttons back. **Alt+Enter** switches between a window and fullscreen
at any moment ([Playing in a window](windowed.md)). Other keys that always work: **F3** shows
and hides the frame rate, **F6** opens PC OPTIONS, **F11** works like Alt+Enter.

## Driving

| Action | Keys |
|---|---|
| Throttle / brake | W / S |
| Steer | A / D |
| Handbrake | Space |
| Gear up / down | E / Q |
| Clutch | Shift |
| Camera | Tab |
| Rewind | R |
| Look around (back: Down) | Arrow keys |
| Look straight back | C |
| Telemetry | T, G |
| Telemetry pages | , / . |
| Pause | Esc |

Driving keys apply while the car is on track and the game is not paused. Steering, pedals
and looking around ramp up while a key is held and ease back on release.

## Menus

The pre-race screen, pause menu, replays and results use these keys too.

| Button | Keys |
|---|---|
| Move | W A S D, arrow keys |
| Select (A) | Enter, Space |
| Back (B) | Esc, Backspace |
| X / Y | X / Y |
| LB / RB | Q / E |
| LT / RT | Z / C |
| Left stick | Numpad 8 4 2 6 |
| Right stick | I J K L |
| BACK / START | Tab / P |

In photo mode, as on a controller, **Numpad 8 4 2 6** (left stick) move the camera, **I J K L**
(right stick) pan it, and **W A S D** or the arrow keys (D-pad) rotate and zoom it.

## Looking around with the mouse

In a race, hold the **right mouse button** and move the mouse left or right: the camera turns
around your car and stays where you leave it. Let go of the button and the camera goes back
behind the car. The mouse pointer is hidden while you drive.

To turn this off or change how fast the camera turns (**MOUSE LOOK SENSITIVITY**, 10 to 100,
25 by default), open the **MOUSE** tab next to KEYBOARD in the game's PC OPTIONS (see
[Rebinding keys](#rebinding-keys) below for how to get there), or the **MOUSE** tab in the
launcher's **CONTROLS** page. Fast flicks swing the camera round smoothly; releasing the
button after a large turn jumps it back behind the car, as releasing a controller's stick does.

## The mouse in menus

In the game's menus the mouse works as in any PC game: what is under the pointer lights up, and a
**left click** chooses it. That goes for menu tiles and rows, colour swatches and vinyl shapes, the
button prompts at the bottom (SAVE, COLOR FINE TUNE...), the tab names and their **Q** / **E**
arrows, and the buttons of a message box. A click on nothing does nothing. With the pointer on nothing,
nothing is lit; the first key or pad press lights the menu again where it was.

**Right click** goes back from menus, and **Esc** or a right click closes a message box that only has OK.
So that it can never answer a question or lose a change, it does nothing in a message box with a choice, on
a page of settings or values (AUDIO, HUD, TUNE SETUP, photo effects), on the Autovista and photo cameras, or
while placing a livery layer: those take a click on their own button.

- **Sliders** (TUNE SETUP): click or drag along the bar.
- **Choices with arrows** (AUDIO): a click on the row picks the next choice, the left arrow the one before.
- **Carousels** (PAINT CAR's tiles, CAR SELECT) keep the chosen tile in the middle: a tile lights under the
  pointer, a click brings it to the middle, and a second click opens it, even with the mouse left where it was.
  A row cut off at a list's end comes into view and opens with one click.
- The **mouse wheel** moves up and down through lists, and along a row of tiles (the main menu, the
  upgrade shop's strip); hold **Shift** to move left and right in a grid, as in the car and track lists.
- **Colours** (livery and paint): the colour under the pointer shows on the car at once; a click picks it.
- **Placing a layer in the livery editor** (MOVE, SIZE, SPIN, SKEW, TRANSPARENCY): drag on the car to
  change the layer the way the tab says (MOVE follows the pointer), turn the **wheel** to size it and
  **Shift** + wheel to spin it. A click on the car does not confirm the layer: OK does.

- **Autovista and photo mode**: left drag walks the camera, right drag turns it, the wheel steps closer or back.

With the mouse in use the prompts are plain text you can click, lit red under the pointer. SELECT goes where a
click on the item does the same, and a side menu's BACK shows the right mouse button. Any key or pad button gives
the usual prompts back.
The keys and a controller work as before, and the mouse is left alone in races.

`--fm4_menu_mouse=false` turns the mouse in menus off for a session ([Launch options](launch-options.md)).

## Rebinding keys

### In the game

1. Open PC OPTIONS (**F6**, **BACK + START** held together on a controller, or Play
   Career → My Profile → Options → PC OPTIONS) and press **RB** (the menu's RB key on the
   keyboard, **E** by default) to reach **KEYBOARD**; **LB** goes back, and clicking a tab's
   name works too. The list has a **DRIVING** section (on the track) and a **MENUS** section.
   The page moves with your menu keys (**W A S D** by default), and always with the arrow keys,
   Enter and Esc.
2. Each row is one controller button with its primary and secondary key. In DRIVING the row
   says what the button does in the controller layout chosen in the game's options, so another
   layout renames the rows.
3. Pick the key with **LEFT** / **RIGHT**, press **A** (Enter, or click it), then press the new
   key. Esc is a key like any other here: **B** on a controller, a click, or five seconds
   without a key backs out. The function keys stay shortcuts and cannot be bound. Shift, Ctrl
   and Alt bind on their own; a combination such as Shift+W only through the file (below).
4. **X** (the menu's X key) clears a secondary key, or on a primary key you set puts the button
   back to its default keys. **RESET ALL** at the bottom sets every key and ramp time back.
5. When you give a button a key another button has by default, the other row shows the key the
   game gives it instead and says why. Two buttons you set to the same key both say "Also used
   by".
6. **SAVE & APPLY** (**Y**) writes `config\controls.toml` and the keys work at once, button
   prompts included. Leaving with unsaved keys asks once more; leaving again discards them.

DRIVING also has the ramp times (see SMOOTHING below), in steps of 0.01 s.

### In the launcher

1. Open the launcher and click **CONTROLS** under Play.
2. Choose **DRIVING** (on the track) or **MENUS**.
3. Click the key you want to change, then press the new key. Each action has a primary key
   and an optional secondary key (**NOT SET** until you click it); the **✕** beside a
   secondary key clears it. **RESET** puts one action back to its default, **RESET ALL** the
   whole list.
4. If you give an action a key that another action has by default, the game takes that key
   off the other action when it starts (it keeps its other key if it has one, otherwise it gets
   a new key), and the panel says so under it, with the lost key dimmed. If you set two
   actions to the same key yourself, both show "Also used by". Nothing stops you: change one
   of them if that is not what you meant.
5. Esc is a key like any other, so it does not back out of a rebind: click **CANCEL** or
   anywhere else instead.
6. The small button names under each action (A, RT and so on) are the game's default
   controller layout. If you chose another layout in the game's options, an action may be on
   a different button than the one named; the in-game tabs follow your layout.
7. Click **SAVE** and start the game. Changes apply at the next launch.

**SMOOTHING** sets how many seconds steering, the pedals and looking around take to reach
full and to return to rest; lower is snappier.

### In the file

Or create `config\controls.toml` in the state folder and add `menu_<button>` or
`driving_<button>` lines. The launcher reads it and leaves the lines of actions you do not
change as they are; changing an action rewrites its line (or removes it when you put the
keys back to the default). A value lists keys separated by commas:

```toml
driving_x = "Q,Numpad1"
```

Changes apply at the next launch.

A key you bind leaves the button it had by default, so it does not do two things (unless you
bind it to two buttons yourself; `Shift+C` counts as C). A button left with no key gets a new
one: the old key of the button you moved there, or else a free key. The button prompts in the
game show where it went, and `runtime.log` lists every move
(`Controls: driving_right_shoulder C -> F`). Bind it yourself to pick a different key.

**Buttons:** `a`, `b`, `x`, `y`, `left_shoulder`, `right_shoulder`, `left_trigger`,
`right_trigger`, `lstick_up`, `lstick_down`, `lstick_left`, `lstick_right`, `lstick_press`,
the same five for `rstick_`, `dpad_up`, `dpad_down`, `dpad_left`, `dpad_right`, `back`,
`start`.

**Key names:** `A`..`Z`, `0`..`9`, `F1`..`F24`, `Up`, `Down`, `Left`, `Right`, `Space`,
`Return`, `Escape`, `Backspace`, `Tab`, `Shift`, `Control`, `Alt`, `Numpad0`..`Numpad9` and
the like.

**Ramps**, in seconds from rest to full and back:

| Key | Default |
|---|---|
| `ramp_steer_seconds` | 0.17 |
| `ramp_steer_return_seconds` | 0.10 |
| `ramp_pedal_seconds` | 0.10 |
| `ramp_pedal_return_seconds` | 0.08 |
| `ramp_look_seconds` | 0.15 |
