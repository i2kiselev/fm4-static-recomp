# Racing wheels

[← Back to the README](../README.md)

A racing wheel can be player one, with the game's own force feedback. The wheel base, the pedals,
an H-shifter, a handbrake and a button box can each be a different USB device: they reach the
game as one Xbox 360 wheel (XInput subtype 2). Devices are read as raw SDL joysticks, so no
list of supported brands is needed.

> [!NOTE]
> Experimental. Tested in unit tests and against the 1.0 code image; not yet in races on every
> wheel. Close tools that take the wheel for themselves (some vendor apps do) before playing.

## Setting it up (launcher → WHEEL)

1. Set the rotation in your wheel's own driver (Thrustmaster, Fanatec, MOZA Pit House, Logitech
   G HUB…): full lock in the game comes at the wheel's full rotation.
2. Tick **ENABLE WHEEL SUPPORT**.
3. **WHAT THE GAME READS** shows, live, what the game gets from your devices. On each row:
   **BIND**, then turn the wheel a quarter turn or press the pedal fully (the steering's device
   becomes the wheel base; a pedal on another USB device is bound with its name), and tick
   **INVERT** if it reads backwards. **×** clears a pedal.
4. **BUTTON BINDINGS** (open it): **BIND**, then press a button, push the hat or pull a lever.
   **+** adds a second input for the same control, **×** clears it. GEAR 1 to 7 and REVERSE are
   an H-shifter's gates.
5. **FORCE FEEDBACK**: choose the force device (the wheel base by default), press **TEST** (it
   pulls left, then right) and tick its **INVERT** if it pulled right first. Start with a low
   **STRENGTH** on a direct-drive base. **EFFECTS** holds each effect's share, the minimum force
   and the rumble.
6. In the game, pick the wheel controller layout. FM4's clutch is a button: choose **MANUAL
   WITH CLUTCH** and give its clutch the button **CLUTCH PRESSES** names (B by default).

Changes are written to `config\wheel.toml` at once and apply the next time the game starts.
While the WHEEL panel is open, the launcher runs the game as `--fm4_wheel_probe` to read the
devices; it sends no force until TEST, so the game can take the force device after.

Some bases appear twice (a Fanatec base has two HID interfaces with the same `0eb7:0004`). Such
a device is then named by its path; if TEST does not move the wheel, pick the other entry.

## Force feedback

FM4 drives a force feedback wheel through five effects: a constant force, a spring, a damper,
friction and inertia, with forces and coefficients at +/-127. Six small XInput-FF functions in
the game send them; while the wheel is player one, hooks answer those functions
(`src/wheel/wheel_hooks.cpp`) and play the effects as SDL haptic effects on the force device,
scaled by STRENGTH and each effect's own share. The forces stop while the game window is in the
background, in menus and pauses (the game's wheel update says whether the player is driving),
and when the game has sent nothing for 200 ms. The game's controller rumble can play as a
vibration on top, on bases that can play one.

The hook addresses were found in the 1.0 code image. The Rev 1 entries in
`config/rexglue/releases/rev1.toml` still need `tools/release_parity.py` against a Rev 1 disc.

## Settings

All in `config\wheel.toml`, also accepted on the command line. Devices are named by `vvvv:pppp`
(USB vendor and product, hex), part of the name, or `path:<SDL path>`; inputs are
`[device|]input`, without a device on the wheel base.

| Setting | Default | Meaning |
|---|---|---|
| `fm4_wheel_enabled` | `false` | The wheel is player one |
| `fm4_wheel_device` | `""` | The wheel base; empty for the first device SDL types as a wheel |
| `fm4_wheel_steering_axis`, `fm4_wheel_steering_invert` | `0`, `false` | Steering axis on the base |
| `fm4_wheel_rotation`, `fm4_wheel_game_rotation` | `900`, `0` | Degrees lock to lock: the wheel's software and the in-game wheel's (900 on 360 is 250 % sensitivity) |
| `fm4_wheel_steering_scale`, `fm4_wheel_steering_deadzone` | `100`, `0` | Sensitivity and dead zone, percent |
| `fm4_wheel_throttle`, `fm4_wheel_brake`, `fm4_wheel_clutch` | `""` | Pedal axes, `[device|][+\|-]axisN` (`-`: rests at the top) |
| `fm4_wheel_buttons` | the hat as the D-pad | `NAME=input,...`: `N` a button, `hN.up` a hat direction, `[+\|-]axisN` an axis past half travel; NAME is A, B, X, Y, LB, RB, LT, RT, LS, RS, BACK, START, UP, DOWN, LEFT, RIGHT, GEAR1-GEAR7 or REVERSE |
| `fm4_wheel_clutch_button` | `B` | Pad buttons the clutch pedal presses past half travel |
| `fm4_wheel_pedal_deadzone`, `fm4_wheel_{throttle,brake,clutch}_deadzone_{bottom,top}` | `3`, `-1`/`0` | Pedal dead zones, percent; bottom `-1` uses the pedal dead zone |
| `fm4_wheel_ffb_device` | `""` | Device that receives forces; empty for the wheel base |
| `fm4_wheel_ffb_mode` | `on` | `on` (the game's forces), `rumble` (vibration only) or `off` |
| `fm4_wheel_ffb_strength` | `25` | Overall strength, percent (0-200; above 100 the strongest forces clip) |
| `fm4_wheel_ffb_min_force` | `0` | Lift small forces, for geared or belted wheels |
| `fm4_wheel_ffb_spring`, `_damper`, `_friction`, `_inertia` | `100` | Each effect's share, percent (0-200) |
| `fm4_wheel_ffb_rumble`, `_rumble_engine`, `_rumble_road` | `50`, `100`, `100` | The rumble as a vibration, and each motor's share |
| `fm4_wheel_ffb_invert` | `false` | Reverse the force direction |
| `fm4_wheel_ffb_periodic_restart` | `false` | Also start every running effect again once a second, for a driver that stops effects on its own; each effect is already started again after every update, which Thrustmaster's driver (T150) needs |
| `fm4_wheel_log_inputs` | `false` | Log raw axis, button and hat changes, to find input numbers |

The SDK's own wheel driver (`wheel_*`, [docs/RACING_WHEELS.md](https://github.com/FugaziScript/shiftglue-sdk/blob/main/docs/RACING_WHEELS.md))
is turned off while this one is on; its binding syntax is the same.
