# Project Forza Plus 4: how the port runs it

[← Back to Project Forza Plus 4](project-forza-plus.md)

The technical side of a [Project Forza Plus 4](project-forza-plus.md) install: which code it
runs, what setup takes from the mod and what it leaves out, and why saves never cross between
vanilla and the mod. Everything here applies to the mod's version 1.3; nothing on this page is
needed to play.

## The code is 1.0

The mod ships its own `Main Game Changes\default.xex`. Its header says version 0.0.0.11 and media
ID `633772A0`, the same as the retail 1.0 Asia Disc 1. Setup uses the executable's SHA-256 hash to
tell them apart. Once unpacked, the mod's code and data match the 1.0 retail executable except for
six bytes: in the resource that lists the game's checked files (below), the name `gamedb` became
`potato`. The mod uses the 1.0 launch code, with no title update.

For the port that means:

- **One translation for vanilla and the mod.** Every analysis address and every game hook of the
  1.0 build is valid for the mod; nothing is translated differently.
- **The install always runs the mod's executable.** Setup puts the mod's `default.xex` into
  `data\game` and builds from it. `config/supported-discs.json` lists it as a release with
  `"mod": true` and code image `1.0`: it is accepted as code to build from, never as a Disc 1.
  Updates expect it there and keep the game folder as it is.
- **Rev 1 discs run 1.0 code on Rev 1 data.** A Rev 1 Disc 1 has only Rev 1 code, so on a mod
  install the mod's executable is its 1.0 code source. Rev 1 is 1.0 with Title Update 2's content
  built in: new string tables, UI scenes and textures, a game database with a level cap of 1000,
  a Le Mans visibility patch. The mod replaces the database with its own (1.0 schema); the rest of
  Rev 1's data stays. The 1.0 code never ran on that data on a console, so it was tested here:
  the full render test suite, career races with a level-up, and both Le Mans layouts, without a
  problem. What else Title Update 2 added is almost all Xbox LIVE, which is closed.
- **Title updates are never applied**, on any install, so the code stays what the mod was made on.

### The checked-files list and the `potato` rename

The executable carries an Xbox SDK resource (`0D163575`) listing 66 files under `media\` with
SHA-256 hashes of each 32 KiB block: the game database, the physics and tunable settings, render
scenarios, string tables, `UI.zip` and more. The game installs it at start and passes every
`game:\media\` file it opens through a lookup that would compare size and hashes and show a disc
error on a mismatch.

That lookup never finds a file: it strips the prefix `game:\media\` and searches the rest
(`db\gamedb.slt`), while the list's names keep `media\` (`media\db\gamedb.slt`). No file is ever
checked, by this port, by Xenia or, as far as the code reads, on a console. The mod's rename of
`gamedb` to keep its database out of the check was not needed, and its other edited files, also
on the list, pass for the same reason. The port adds no guard for it.

## What setup takes from the mod

`config/pfp4-1.3.json` (made by `tools/make-pfp4-manifest.py` from the mod) lists every file an
install takes, with its size and SHA-256. Setup reads the zip entry by entry, never extracting it
whole, or an extracted folder; it finds the mod's root by `Main Game Changes\default.xex`, up to
two folders deep. Each file is hashed while it is copied, a file of the wrong size is not copied
at all, and any mismatch refuses the whole mod: a different version of it changes nothing.
Everything is written into `.partial` folders first and moved into place only when complete, so
an interrupted setup simply runs again.

`Project Forza Plus 4 Update v1.3.zip` is the author's patch for installs of older mod versions:
all 669 of its files are identical to the main zip's, and it holds no executable.

| From the mod | Where it goes | Notes |
|---|---|---|
| `default.xex` | `data\game` | The 1.0 code, above. |
| `PhysicsSettings.ini`, `PI.xml` | `data\game\Media\physics` | Traction and stability control, clutch and shift times, aero, camber and tyre pressure limits; performance index weights. `CarPhysicsUpdateHz` stays 359, as in vanilla. |
| `GameTunableSettings.ini` | `data\game\Media\GameTunableSettings` | Earnings, sell value, AI auto-upgrades; the Harder AI variant replaces it. |
| `TireSmoke.xml`, `Backfire.xml` | `data\game\Media\Effects` | Thicker, longer tyre smoke (its particle budget goes from 200 to 1000) and longer backfire flames. |
| `AudioEngineConfig.xml`, `BackfireTimerConfig.xml` | `data\game\Media\audio` | Mixer volumes, exhaust sound, backfire timing. |
| The mode's `gamedb.slt` | `data\game\Media\db` | Cars, parts, physics data, AI, events and career of the chosen mode. |
| 20 content packages, 9.3 GB | `data\content` | Disc 2's four packages and 16 DLC packs, below. |
| Every mode's databases and Skip Intro saves | `data\pfp4` | Kept for later mode changes, 0.25 GB. |

### Content packages

- **Disc 2:** the mod's four Disc 2 packages are byte for byte the disc's (cars and wheels only,
  no tracks), so a mod install never asks for Disc 2.
- **DLC:** in 16 packs the mod ships the `<n>_pri_65.zip` archive extracted, so that its own
  per-mode car databases (`*_merge.slt`) win over the copies inside the zip. Every car model and
  texture is unchanged. The mod holds every DLC pack, so CONTENT offers no DLC to add on a mod
  install.
- **Package headers:** the runtime lists installed content from one header file per package
  (display name, licence mask), which an install from a Marketplace package writes. The mod's
  folders come without them, and without them the game closes when it first opens content.
  Setup writes them from the manifest, which holds only those names and masks.
- **One patched file:** pack 15 (Launch Track Pack) carries a `TrackSettings.xml` with the same
  Xenia workaround as the tracks below. Setup puts its three values back (shadow colour,
  specular level, road detail blur) and checks the result's hash against the retail file.

## What is left out

| Mod files | Why |
|---|---|
| 36 track `TrackSettings*.xml` files and 6 showroom `TrackSettings.xml` | Xenia workarounds: white shadow colour (no shadows), no specular, no road detail blur; in the showrooms, image-based lighting off. This port draws all of these. |
| 21 `RenderScenarios\*.xml` | 60 fps post-race screens, replays and photo mode, and full-rate mirrors in 12- and 16-car races. The port already draws those scenes at full rate, and loading the files would stop the Console and High presets from giving back the console's half-rate mirror. |
| `PhysicsSettings.ini for higher FPS` | One file per fixed frame rate; the port keeps the AI's physics steps at any rate itself (below). |
| `xenia-canary.config.toml` | Xenia's own settings: a 16 ms vblank and every DLC licence unlocked. |

## Career modes

Each of the four modes (Normal, Hard, Sandbox, Vanilla) has its own game database and its own
car databases for the 15 DLC packs that carry one. Its Harder AI variant has another database,
in which only the AI drivers' skills differ, and its own `GameTunableSettings.ini` with stronger
AI upgrades. Setup copies every mode into `data\pfp4\modes`, so SETTINGS → PROJECT FORZA PLUS 4
can switch without the mod: it checks every file of the new mode against the manifest before it
copies one, writes the copies beside the game's files and renames them over all at once, so a
damaged cache or an interrupted switch leaves the current mode whole.

The part tables are identical in all four modes (Sandbox changes only prices), so a garage
survives a switch. The career structure does not: races, events and level rewards differ, which
is why a career loads with a warning, once, in another mode than its own.

## Saves

The mod keeps the database's schema and every one of the game's 503 car ids, but not its upgrade
parts. A garage row holds the ids of the parts bought for that car, and many vanilla ids are gone
in the mod:

| Vanilla parts | Missing in the mod |
|---|---|
| Intercoolers | 256 (245 of them under new ids) |
| Single turbos | 126 |
| Engine swaps | 44 |
| Twin turbos | 39 (3 of them stock parts) |
| Drivetrain swaps | 11 |
| Superchargers | 6 |

A vanilla career loaded on the mod was tried: most missing parts fall back to stock, but a car
with an engine swap breaks (the career hangs, a race loads grey). The other way, a mod career on
vanilla can point at any of about 2,400 part ids vanilla does not have. Each car's cached performance index
and class also stay the old game's until a part changes, and the mod moves every class boundary.
The careers' races and rewards differ too. Hence the rules:

- Each profile records its game (`game` in its `profile.toml`) and the mode it last ran in.
  IMPORT SAVE refuses the other game's saves; a save from outside the launcher is judged by what
  the player declares, with a warning when Xenia's DLC folder beside it says otherwise (the mod
  keeps its DLC archives extracted, vanilla keeps the zips).
- A mod install takes the mod's Skip Intro saves as ready-made careers. They were made on Xenia
  with the profile id this port signs in with (`B13EBABEBABEBABE`), so they load as they are. Their
  garage pictures come from the author's own save, so the starter car's picture is wrong until it
  is repainted once.

## Physics substeps and the AI

The game steps each car in physics substeps: `CarPhysicsUpdateHz` (359) times the tick's length,
rounded up. At the console's 60 fps that is six substeps a tick; at 120 fps three, at 240 two.
Vanilla's AI covers the same distance at any of these rates, but the mod's AI was tuned on Xenia,
whose 16 ms vblank runs the game at about 62 fps with six substeps a tick, and it misses corners
with fewer.

The mod's FPS files keep the substeps per tick by raising the rate for one fixed frame rate:
`CarPhysicsUpdateHz` = frame rate x 5.79 (359 / 62), e.g. 695 at 120 fps. FRAME RATE here changes
at any moment, also mid-race, so a fixed file cannot follow it. The port sets a floor instead:
on a mod install the launcher passes `--fm4_car_min_substeps=6`, and a hook in the car update
raises any car's substep count for that tick to at least six. It applies where the game uses the
ini's rate, the cars the mod's files reach; a long tick after a hitch still takes the extra
substeps its length needs. At 120 fps the physics then do twice the console's work per second,
at 240 four times. In one player test of the first World Tour race at 120 fps, AI crashes went from up
to three cars at once to one.

## Testing

Tested on mod installs from the 1.0 PAL, Rev 1 Europe and Rev 1 USA discs: new careers and Skip
Intro careers, races and saving, mode switches both ways, Disc 2 and DLC cars, and the imports'
refusals and warnings. The full render test suite passes on mod installs from 1.0 PAL and Rev 1
USA except 3 checks that compare a garage screen with vanilla's pictures: the mod's
car stats and prices differ there, the car itself draws the same.
