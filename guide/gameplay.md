# Gameplay settings

[← Back to the README](../README.md)

The GAMEPLAY tab of the in-game PC OPTIONS page holds settings that change how the game plays.
Open PC OPTIONS (**F6**, **BACK + START** held together on a controller, or Play Career → My
Profile → Options → PC OPTIONS) and press **RB** (the menu's RB key on the keyboard, **E** by
default) until **GAMEPLAY** is shown; **LB** goes back, and clicking a tab's name works too.

## World Tour narrator

**WORLD TOUR NARRATOR** set to **MUTED** silences the voice that introduces each World Tour event
and track in the career menus, in every language. Jeremy Clarkson's Autovista commentary is not
affected. On by default; SAVE & APPLY switches it at once, no restart needed.

It works on the game's narrator voice bank (`Media\audio\VO\<language>\vo.fsb`) without
changing the file: each line is cut to a few milliseconds as the game reads the bank, and the
bank already loaded is changed in memory when you switch it.

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
