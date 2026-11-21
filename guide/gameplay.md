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

## Career difficulty

The **CAREER DIFFICULTY** rows change how the game itself plays, to make the career harder. Each is off (the
game's own behaviour) by default, and each only ever makes a race harder.

They work on the game's own data as it loads it: with an option on, the game is given an edited
copy of its database (`Media\db\gamedb.slt`) or of its tunables
(`Media\GameTunableSettings\GameTunableSettings.ini`). The files on disk are never changed,
so turning an option off again, or a mod changing those files, works as before. The game reads
them as it starts, so a change takes effect **from the next start**; the row says so until then.

Each change is made to the values the files hold, so on top of a mod's own (PFP4's lower
payouts, for one). A value stays in as many digits as the file gives it: where that is one
decimal (the disc's resale value, 0.5), -25% and -50% come out a little smaller, about -20% and
-40%. The log names each option applied with the values it wrote.

With RACE PAYOUTS on, the World Tour event panel still shows the event's full payout; the
earnings screen after a race pays the reduced amount.

| Setting | Choices | What changes |
|---|---|---|
| **RACE PAYOUTS** | DEFAULT, -25%, -50% | The credits a race pays by distance (`RaceWinnings\CreditsPerMile`) and the car makers' affinity level bonuses (`AffinityLevelDef`) |
| **REPAIR COSTS** | DEFAULT, +100%, +200% | The share of a race's winnings each damaged part costs (`RaceWinnings\DmgPenalty*`), a part at most the whole winnings |
| **RESALE VALUE** | DEFAULT, -25%, -50% | What selling a car pays (`CarValueScales\SellValueScale`, `RestockingValueScale`) |
