# FM4 Static Recomp

[![Latest release](https://img.shields.io/github/v/release/i2kiselev/fm4-static-recomp?label=release)](https://github.com/i2kiselev/fm4-static-recomp/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/i2kiselev/fm4-static-recomp/total)](https://github.com/i2kiselev/fm4-static-recomp/releases)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20ARM64-blue)](#what-you-need)
[![License](https://img.shields.io/badge/license-BSD--3--Clause-green)](LICENSE)

[![NVIDIA DLSS](https://img.shields.io/badge/NVIDIA-DLSS-76B900?logo=nvidia&logoColor=white)](guide/upscalers.md)
[![AMD FSR 1, 2 and 3](https://img.shields.io/badge/AMD-FSR%201%20%7C%202%20%7C%203-ED1C24?logo=amd&logoColor=white)](guide/upscalers.md)
[![Mod: Project Forza Plus 4 1.3](https://img.shields.io/badge/mod-Project%20Forza%20Plus%204%201.3-orange)](guide/project-forza-plus.md)

An unofficial static recompilation of the Xbox 360 release of *Forza Motorsport 4* for
Windows, with an experimental Apple Silicon macOS preview, built locally from your own disc.

**Mac users:** see the [macOS guide](guide/macos.md) for setup and testing status.

The game's PowerPC code is translated ahead of time into C++ by the
[ShiftGlue SDK](https://github.com/arcanite24/shiftglue-sdk) and compiled for x86-64 on Windows or ARM64 on macOS. Its GPU
commands run on ShiftGlue's native Vulkan renderer, written for
[Pinyon Shift](https://github.com/arcanite24/pinyon-shift) (*Forza Horizon*) and extended
here for FM4.

## New in 0.3.2: keyboard rebinding and many audio improvements

> [!TIP]
> **[Rebind the keyboard](guide/controls.md#rebinding-keys)** in the game or in the launcher,
> with a primary and a secondary key for each action. **The sound got a lot of work:**
> [your own music](guide/launch-options.md#custom-soundtrack) in the menus and in races,
> engine sounds without pops, and a faster audio path for older processors. Also new: stronger
> vibration for controllers that vibrate too weakly, DLC that the game showed as locked works
> now, GPU readbacks and car thumbnails are much more reliable (a car turning pink after a
> change of home space, or a thumbnail missing a tile, should now be rare), the first start
> prepares shaders before the title with a progress bar, and there is an
> [experimental preview for Apple Silicon Macs](guide/macos.md). Everything else in this version:
> [release notes](https://github.com/i2kiselev/fm4-static-recomp/releases/latest).
>
> Since 0.3.1, **[Project Forza Plus 4](guide/project-forza-plus.md)** sets up from the launcher
> **as a new install of its own**, and the **Rev 1** (1.1) Disc 1 releases work.

> [!NOTE]
> **Experimental release.** The offline game modes play without known blocking problems
> (see [Tested](guide/performance.md#tested)). This repository contains **no** game code or
> assets: you need your own legally obtained copy of the game (see [Legal](#legal)).

| 3840x2160 (3x), 120 fps | 2560x1440 (2x), 160 fps |
|---|---|
| [![Nürburgring GP, BMW M3 E92, 2160p at 120 fps](https://img.youtube.com/vi/Kf01UV4NT_Y/maxresdefault.jpg)](https://www.youtube.com/watch?v=Kf01UV4NT_Y) | [![Hockenheimring Short, BMW M5, 1440p at 160 fps](https://img.youtube.com/vi/nxM1L6pbW0U/maxresdefault.jpg)](https://www.youtube.com/watch?v=nxM1L6pbW0U) |
| Nürburgring GP, BMW M3 E92 | Hockenheimring Short, BMW M5 |

<sub>YouTube plays video at up to 60 fps; the frame rates are the game's during the recording.</sub>

## Quick start

1. Download `FM4StaticRecomp-Launcher.zip` from the
   [latest release](https://github.com/i2kiselev/fm4-static-recomp/releases/latest) and extract
   it into a short folder you own, for example `D:\FM4`.
2. Start `FM4StaticRecompLauncher.exe` and follow its setup steps: the game (vanilla or
   Project Forza Plus 4), Disc 1, Disc 2 (optional; on Project Forza Plus 4 the mod's zip
   instead), the Microsoft licence, the build and your profile.
3. Press **PLAY**.

> [!NOTE]
> The first start prepares shaders for your GPU before the title screen, with a progress bar.
> The window stays responsive; the time depends on your computer and graphics driver. Completed
> work is saved every 10 seconds and when preparation finishes, so later starts are much faster.
> An update or a GPU driver update may require preparation again. You can close the window
> during preparation; it may briefly wait for the current graphics operation to finish.

The first setup takes about 10 minutes (under 8 on the [test bench](guide/performance.md#test-bench)).
Details: [Launcher guide](guide/launcher.md). Prefer
the command line? See the [manual install](guide/manual-install.md).

## Documentation

| Guide | What's in it |
|---|---|
| [macOS](guide/macos.md) | Experimental Apple Silicon setup, requirements and testing status |
| [Launcher](guide/launcher.md) | First setup, content, unicorn cars, starting from Steam or a frontend, the `data` folder, updates |
| [Profiles and saves](guide/saves.md) | Profiles, importing saves and Xenia Canary careers, a saves folder of your own |
| [Playing in a window](guide/windowed.md) | WINDOWED and WINDOW SIZE, Alt+Enter, resizing, upscalers in a window |
| [Manual install](guide/manual-install.md) | Command-line setup, Disc 2 and DLC, updating |
| [Project Forza Plus 4](guide/project-forza-plus.md) | Setting up the mod: its own install, career modes, saves, what is left out |
| [Project Forza Plus 4: how the port runs it](guide/project-forza-plus-internals.md) | The mod's 1.0 code, also on Rev 1 discs, what setup takes and patches, why saves don't mix, the AI's physics steps |
| [Graphics settings](guide/graphics.md) | The in-game GRAPHICS page, presets, what each setting does |
| [Upscalers](guide/upscalers.md) | DLSS, FSR 2, FSR 3 and FSR 1: what to pick, how the port feeds them, known limits |
| [High frame rates](guide/high-frame-rate.md) | Picking a frame rate, how the game runs at up to 240 fps at real speed, limits |
| [Keyboard](guide/controls.md) | Key layout for driving and menus, rebinding |
| [Racing wheels](guide/racing-wheels.md) | A wheel, pedals and shifter from any USB devices, binding them in the launcher, force feedback |
| [Launch options](guide/launch-options.md) | Command-line options, languages, `-Compat`, frame generation, setting options, logs and crash reports |
| [Debug options](DEBUG_OPTIONS.md) | Debug views, A/B switches of rendering fixes and diagnostics |
| [Testing and benchmarks](guide/performance.md) | What was played, frame rates and VRAM, comparisons with Xenia and hl360-forza4 |
| [Common problems](guide/troubleshooting.md) | Project Forza Plus 4 next to a vanilla install, crashes from overclocks, stutter, sound on older CPUs, upscalers that stay off, and their fixes |
| [Reporting issues](guide/reporting-issues.md) | Checklist and template for a bug report |

## Features

Compared with the original console, and with the same translation built against the stock
ShiftGlue SDK (without this project's game hooks and renderer changes):

| | Xbox 360 | Stock ShiftGlue SDK | FM4 Static Recomp |
|---|---|---|---|
| Resolution | 1280x720 | 1x to 4x of 720p, but the road and car shadows render wrong | 1x to 4x of 720p (up to 5120x2880), switchable live |
| Race frame rate | 60 fps | The console's pacing | 60 fps up to the display's refresh rate (at most 240) ([how](guide/high-frame-rate.md)) |
| Menus and showroom | 60 / 30 fps | The console's pacing | The race frame rate; animations and the showroom camera run at real time |
| Race loading | Console speed | Console speed | Faster |
| Sun and lights | Correct | Screen-filling glare | Correct |
| Anti-aliasing | The game's 2x/4x MSAA | The game's MSAA | Showroom and race MSAA 4x, 2x or off each, FXAA, FXAA Extreme, DLAA |
| Upscaling | - | DLSS, FSR 2 and FSR 3 with Forza Horizon's camera tracking, which does not fit FM4 | **DLSS, FSR 2 and FSR 3** with FM4's own depth, camera cuts and per-object motion ([how](guide/upscalers.md)) |
| Window | - | A launch option | [WINDOWED](guide/windowed.md) in the launcher, **Alt+Enter** while playing |
| Graphics settings | Fixed | Launch options only | In-game [GRAPHICS page](guide/graphics.md) (**BACK + START** or **F6**) with presets and per-setting switches, saved between sessions |
| VRAM at 3x | - | About 8 GB | About 2 GB |
| Input | Controller | XInput gamepad | XInput gamepad and **[keyboard](guide/controls.md)**, with keycap button prompts and keys rebound in the game or the launcher |
| Disc 2 and DLC | From the disc and Marketplace | Not wired up | Installed at launch from folders you name |
| Mods | - | - | [Project Forza Plus 4](guide/project-forza-plus.md) 1.3, set up from its zip |
| Frame generation tools | - | Not visible to them | [Opt-in](guide/launch-options.md#frame-generation) for Lossless Scaling and NVIDIA Smooth Motion |
| Xbox LIVE, Kinect | Yes (LIVE servers closed) | No | No |

More than twice Xenia's frame rate at every scale on the same PC
([benchmarks](guide/performance.md#compared-with-xenia)).

## Supported game

| | |
|---|---|
| Game | Forza Motorsport 4, Xbox 360, retail, title ID `4D530910` |
| Disc 1, 1.0 PAL | Europe, Australia, media ID `61D62D9F` |
| Disc 1, 1.0 USA | USA, media ID `26FF1101` |
| Disc 1, Rev 1 Europe | The 1.1 re-release, Europe, media ID `42C67824` |
| Disc 1, Rev 1 USA | The 1.1 re-release, USA, media ID `33E7B39F` |
| Disc 2 | Optional: the install disc's four content packages (more cars) |
| DLC | Optional: Marketplace packages for title `4D530910` |
| Mod | Optional: [Project Forza Plus 4](guide/project-forza-plus.md) 1.3 over any Disc 1 above, only in a new install of its own: a vanilla install cannot be switched to it |

The two 1.0 releases carry the same code, and so do the two Rev 1 releases; only their
executables' headers differ. Setup recognises the release from `default.xex` and translates
that release's code, so the game folder must keep code of the disc it was set up from: the game
refuses to start on a folder of the other code (1.0 or Rev 1) and asks to run setup again. Any
other version of `default.xex` will not work (except Project Forza Plus 4 1.3's, which is the 1.0
code), because the translation is made for exact code; title updates are not applied. Other
releases are untested. `config/supported-discs.json` lists every Disc 1
file with its size and hash.

## What you need

- Windows 10 version 2004 (build 19041) or newer, 64-bit. On a Mac, see the
  [macOS guide](guide/macos.md).
- A GPU with a current Vulkan driver.
- An XInput gamepad (an Xbox controller, or another pad through a mapper), or the keyboard.
- Your own Forza Motorsport 4 Disc 1, as an ISO or a Games on Demand (GOD) copy. Disc 2 and
  DLC are optional; so is Project Forza Plus 4 1.3's zip, for an install of the mod.
- An internet connection for the first setup. No administrator permission and no Visual
  Studio: setup downloads the parts of Microsoft's C++ runtime and Windows SDK the build
  needs (about 300 MB) once you accept their licences.
- About 10 GB of disk space (Disc 1 7.7 GB, the build tools with Microsoft's files 1.4 GB, the
  source with the prebuilt SDK and the build 1 GB), plus about 5 GB for Disc 2 and DLC; about
  20 GB for a Project Forza Plus 4 install. The game
  installs into a `data` folder next to the launcher, so put the launcher folder on the drive
  that has the space before you start it; the setup page shows where it installs.
- About 10 minutes for the first setup (under 0.5 GB to download, most of it from Microsoft);
  a rebuild after an update takes a few minutes.

## Known issues

Known problems are tracked as
[open issues](https://github.com/i2kiselev/fm4-static-recomp/issues); fixes for the common ones are in
[Common problems](guide/troubleshooting.md). Found a new one? See
[Reporting issues](guide/reporting-issues.md).

## Roadmap

1. **Upscaler polish**: the remaining shimmer (see
   [Known limitations](guide/upscalers.md#known-limitations)).
2. **Racing wheels**: experimental support is in ([Racing wheels](guide/racing-wheels.md)); more wheels to test.
3. **Linux**: a native build of the game and the launcher.
4. **Other disc releases**, such as the Essentials Edition.
5. More tests on niche or legacy hardware.

## Repository layout

<details>
<summary>Where things are</summary>

| Path | What it is |
|---|---|
| `src/` | The host app: game hooks, built-in defaults, Disc 2 and DLC install, the GRAPHICS page and its KEYBOARD tab |
| `src/input/` | Keyboard play: bindings, driving smoothing, keycap prompts |
| `tests/` | Unit tests of the host app |
| `config/rexglue/` | The recompiler's input: function analysis and hooks at their 1.0 addresses, each release's fragment list, the manifest |
| `config/rexglue/releases/` | Where each 1.0 address is in Rev 1's code, with a fingerprint of the code there (`tools/port_release.py`, checked by `tools/release_parity.py`) |
| `config/*.json` | The pinned toolchain and SDK, the supported discs' file lists, Project Forza Plus 4 1.3's file list (`pfp4-1.3.json`, made by `tools/make-pfp4-manifest.py`) |
| `tools/setup.ps1`, `build.ps1`, `codegen.ps1`, `run.ps1` | Setup, build and launch |
| `launcher/` | The launcher (Avalonia, .NET 8): its core library with tests, and the app |
| `tools/package-release.ps1` | Builds the release assets and the players' zip |
| `config/shaders/` | The pipeline list and vertex shader table setup builds the shader cache from |
| `tools/shader_list.py`, `tools/warm-up.ps1`, `config/warm-up/` | Development tools that make that list: a scripted warm-up, its profile, list merging and checks |
| `DEBUG_OPTIONS.md` | Debug views, A/B switches of rendering fixes and diagnostics |
| `tools/` (the rest) | Development tools; several assume the author's folder layout |
| `guide/` | The player guides linked above |

</details>

## Credits

- [Pinyon Shift](https://github.com/arcanite24/pinyon-shift) by arcanite24: the
  *Forza Horizon* recompilation whose native renderer, host UI and build scripts this
  project builds on (see `THIRD_PARTY_NOTICES.md`).
- [ShiftGlue SDK](https://github.com/arcanite24/shiftglue-sdk), Pinyon Shift's fork of the
  [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk): the recompiler and runtime. This
  project uses [a fork of it](https://github.com/i2kiselev/shiftglue-sdk) (branch
  `fm4-static-recomp`) with FM4's renderer fixes.
- [Xenia](https://xenia.jp), from which much of the runtime and GPU emulation in ReXGlue
  derives.
- [fm4-recomp](https://github.com/GabrielCRadu/fm4-recomp), an earlier FM4 recompilation
  on ReXGlue, which inspired this project.
- [Project Forza Plus 4](https://www.reddit.com/user/Gas-and-Games/comments/17b9k70) by
  Gas-and-Games: the car, career and physics mod the launcher sets up. This repository holds
  none of its files; you download the mod from its author.

The people whose pull requests or testing are part of the project are listed in [CONTRIBUTORS.md](CONTRIBUTORS.md).

**AI use:** code, scripts, documentation and reverse engineering of the game binary are
AI-assisted.

## License

The source in this repository is under the BSD 3-Clause License (`LICENSE`). Parts adapted
from Pinyon Shift keep their own BSD 3-Clause notice (`THIRD_PARTY_NOTICES.md`). The license
covers this project's code only: the game, its assets and any executable built from its
translation are not covered, and must not be redistributed.

## Legal

This is an unofficial fan project. It is not affiliated with, endorsed, sponsored or
approved by Microsoft Corporation, Xbox Game Studios, Turn 10 Studios, or any other company
or person that made, published or licensed *Forza Motorsport 4*, including the car
manufacturers, track owners and other licensors whose names and brands appear in the game.
*Forza*, *Forza Motorsport*, *Xbox* and *Xbox 360* are trademarks of Microsoft Corporation;
every other trademark belongs to its owner. This repository contains **no** game code, game
assets, disc images, generated translation or prebuilt game executable: you need your own
legally obtained copy of the game, and the executable is built on your computer.
