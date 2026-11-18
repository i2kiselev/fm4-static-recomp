# Gameplay settings

[← Back to the README](../README.md)

The GAMEPLAY tab of the in-game PC OPTIONS page holds settings that change how the game plays.
Open PC OPTIONS (**F6**, **BACK + START** held together on a controller, or Play Career → My
Profile → Options → PC OPTIONS) and press **RB** (the menu's RB key on the keyboard, **E** by
default) until **GAMEPLAY** is shown; **LB** goes back, and clicking a tab's name works too.

## Quick restart

**QUICK RESTART** restarts the race from the track with one button, without the pause menu:
the screen fades to black, the game's loading screen shows while the race reloads, and the race
comes back on a short start sequence (3, 2, 1, GO in about three seconds instead of the full
intro). It has been played in Free Play races, World Tour events and Hot Lap; a pause menu
without a RESTART is closed again and nothing else happens. Off by default; SAVE & APPLY switches it at once, no restart needed.

- **QUICK RESTART BUTTON**: **D-PAD LEFT** or **D-PAD RIGHT**. No controller layout uses
  them while driving, so the button does nothing else. A spare button on your pad (a back
  paddle) mapped to one of them in its own software makes a dedicated restart button.
- **QUICK RESTART TRIGGER**: **HOLD** (the default) restarts once the button has been held
  while the screen fades out; let go during the fade to cancel. **TAP** restarts on one press,
  with no cancel.

It only works while driving: not in menus, replays or photo mode, and not during a start
sequence until GO has left the screen. Races started from the menus, and restarts from the
pause menu, keep the game's full intro. The keyboard has no quick restart key.

## Quick Race laps (experimental)

**QUICK RACE LAPS** sets the number of laps of every Quick Race, from 1
to 99, on every track; DEFAULT keeps each track's own count. It is saved to
`config\gameplay.toml` and takes effect the next time track select opens: its LAPS box
shows the new count. Kinect races keep their own count
([#75](https://github.com/i2kiselev/fm4-static-recomp/issues/75)).

Races of up to 55 laps have been played to the finish
([#102](https://github.com/i2kiselev/fm4-static-recomp/issues/102)) with steady frame rate and
memory use; the AI races the whole distance. What to expect on a long race:

- Tyres do not wear and no fuel is used, for you or the AI.
- Cars still on track when you finish keep racing behind the results screen until each one
  completes its laps; with lapped cars this can take many minutes.
- The replay does not cover the whole race: in a 55-lap race it ended at lap 27.
- Rewinding more than about 25 laps back shows a loading screen instead of rewinding on
  screen; it still takes you back.
