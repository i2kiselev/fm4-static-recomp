# Gameplay settings

[← Back to the README](../README.md)

The GAMEPLAY tab of the in-game PC OPTIONS page holds settings that change how the game plays.
Open PC OPTIONS (**F6**, **BACK + START** held together on a controller, or Play Career → My
Profile → Options → PC OPTIONS) and press **RB** (the menu's RB key on the keyboard, **E** by
default) until **GAMEPLAY** is shown; **LB** goes back, and clicking a tab's name works too.

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

## Fast post-race

**FAST POST-RACE** plays two of the waits around a World Tour race fast, without skipping
anything: each still plays to its normal end.

- **GLOBE**: the World Tour globe, which spins to the next event and zooms down to its track,
  takes about a second on opening World Tour (instead of about 9) and about two after a
  finished event (instead of about 12).
- **GLOBE + XP BARS**: the post-race XP and car affinity bars also fill in under a second
  (instead of about 6).

Level-up rewards, the championship result, gift cars and every other screen are unchanged.
Off by default; a change applies at once.
