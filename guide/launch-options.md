# Launch options

[← Back to the README](../README.md)

Pass these after `tools\run.ps1`'s own parameters (see the [manual install](manual-install.md#4-play)),
or in the launcher's SETTINGS.

| Option | Meaning |
|---|---|
| `--resolution_scale=1..4` | Internal resolution, a multiple of 1280x720 |
| `--fm4_frame_rate=60..240` | Frame rate of races, menus and the showroom |
| `--vulkan_present_mode=immediate\|fifo` | VSYNC off or on |
| `--swap_post_effect=none\|fxaa\|fxaa_extreme` | Post-process anti-aliasing |
| `--present_effect=bilinear\|fsr2\|fsr3\|dlss` | UPSCALER: off, FSR 2, FSR 3 or DLSS |
| `--present_upscaler_sharpness=0..100` | SHARPNESS after the upscaler, in percent |
| `--present_dlss_preset=auto\|k\|m\|j\|l\|default` | DLSS PRESET |
| `--fm4_disc2_packages=<folder>` | Install the Disc 2 packages from this folder |
| `--fm4_dlc_packages=<folder>` | Install every DLC package under this folder |
| `--fm4_skip_intro=true` | Skip the startup disclaimer and studio videos |
| `--fm4_language=<code>` | The game's language (see [Languages](#languages)) |
| `--fm4_config_root=<folder>` | Where `graphics.toml` and `controls.toml` live (default: the state folder's `config`) |
| `--fh1_async_readbacks=false` | Turn off asynchronous readbacks, if you ever see a corrupted texture |
| `--vulkan_present_via_dxgi=true` | Present through DXGI, for [frame generation tools](#frame-generation) |
| `--pad_chord_game_menu=BACK+START` | Controller buttons held together that open the GRAPHICS page (`A`, `B`, `X`, `Y`, `LB`, `RB`, `LS`, `RS`, `BACK`, `START`, `UP`, `DOWN`, `LEFT`, `RIGHT` joined by `+`); empty turns it off |

## Languages

`--fm4_language` takes `en-US` (default), `en-GB`, `fr`, `de`, `it`, `es`, `es-MX`, `pl`,
`ru`, `ja`, `ko`, `zh-TW`, `pt-BR`, `cs`, `hu` or `nl`.

The game shows every language on Disc 1. Korean, Traditional Chinese, Brazilian Portuguese,
Czech, Hungarian and Dutch have no voice-over of their own and keep the English one. With the
keyboard, some prompts keep the controller's wording in languages other than English.

## `-Compat`

`tools\run.ps1 -Compat` turns off every rendering shortcut of this project and uses the
console's graphics settings. If something looks wrong, compare with a `-Compat` launch: if it
looks the same there, it is most likely the game's own look.

## Frame generation

The built-in 2x backends are selected with `--present_frame_generation=off|dlss|fsr`,
`--present_frame_generation_multiplier=2` and `--present_reflex=off|on|boost`. They require the
Vulkan renderer and a restart. See [Frame generation and Reflex](frame-generation.md).

Lossless Scaling and NVIDIA Smooth Motion do not see frames presented through the Vulkan
swapchain. `--vulkan_present_via_dxgi=true` presents through DXGI instead, and both tools then
work. It is off by default because Smooth Motion's frame pacing does not look right with it
yet.

**Debug and A/B options**: switches of individual rendering fixes, debug views of the
upscaler's inputs and renderer diagnostics are listed in [DEBUG_OPTIONS.md](../DEBUG_OPTIONS.md).

## Setting options and debugging

Every option above and in [DEBUG_OPTIONS.md](../DEBUG_OPTIONS.md) is a setting of the game's
executable, written `--name=value`; an on/off option also takes `--name` for true and
`--no-name` for false (`--no-fm4_skip_intro`). There are four ways to set one; the examples set
`present_upscaler_view` (a debug view of the upscaler's motion input) and
`present_upscaler_object_motion`.

### With the launcher: COMMAND-LINE OPTIONS

SETTINGS → ADVANCED → **COMMAND-LINE OPTIONS** takes options as you would type them after the game on
a command line, separated by spaces; quote a value with spaces (`--fm4_dlc_packages="D:\My DLC"`):

```text
--present_upscaler_view=motion --present_upscaler_object_motion=false
```

SAVE keeps them for every start, until you clear the field. They are added to the game's command
line, so they beat every other way below; an option the launcher passes itself (a SETTINGS row,
COMPAT MODE's list, even a path) is replaced by yours, not repeated. `data\logs\launcher.log`
lists them at each start.

### With the launcher: environment variables

The game the launcher starts inherits its environment, and the
game reads every option from a variable named `REX_` plus the option's name in capitals:
`present_upscaler_view` is `REX_PRESENT_UPSCALER_VIEW`. Save this as `debug.bat` beside
`FM4StaticRecompLauncher.exe`, **close the launcher**, and start `debug.bat`:

```bat
@echo off
set "REX_PRESENT_UPSCALER_VIEW=motion"
set "REX_PRESENT_UPSCALER_OBJECT_MOTION=false"
start "" "%~dp0FM4StaticRecompLauncher.exe"
```

The variables reach only the launcher that `debug.bat` starts, and the games it starts; a
launcher that is already open keeps its own environment. To go back, close the launcher and
start it as usual. From PowerShell the same is:

```powershell
$env:REX_PRESENT_UPSCALER_VIEW = 'motion'
& D:\FM4\FM4StaticRecompLauncher.exe
```

### A config file: `fm4_static_recomp.toml`

The game reads `fm4_static_recomp.toml` from the folder of `fm4_static_recomp.exe`, one
`name = value` per line, strings in quotes:

```toml
present_upscaler_view = "motion"
present_upscaler_object_motion = false
```

That folder is `out\build\win-amd64-release` in a manual install, and
`data\source\<version>\out\build\win-amd64-release` under the launcher, where each update
builds a new version folder without the file: copy it over after an update.

### The command line (manual install)

`tools\run.ps1` passes everything after its own parameters to the game:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run.ps1 -GameRoot D:\FM4\disc1 `
    --present_upscaler_view=motion --present_upscaler_object_motion=false
```

`run.ps1` also works in a launcher install, from `data\source\<version>` with
`-GameRoot ..\..\game`, but it then plays with its own save, settings and shader cache in
`.local\state` there, not with a launcher profile.

### Which value wins

From lowest to highest: the built-in default, `fm4_static_recomp.toml`, an environment variable,
the command line. Two exceptions:

- **GRAPHICS page settings** (`present_effect`, `resolution_scale`, `fm4_race_msaa` and the
  other rows) are saved to `graphics.toml`, which is applied after all three and beats the
  file and environment variables; only the command line beats it. Change those on the
  GRAPHICS page or in the launcher's SETTINGS. `graphics.toml` (`data\config\graphics.toml`
  under the launcher, `.local\state\config\graphics.toml` in a manual install) belongs to that
  page, not to the options above: do not add other options to it.
- **The launcher's own options** are passed on its command line and beat environment
  variables: the paths, `fm4_language`, `fm4_skip_intro`, and COMPAT MODE's list. Set those in
  its SETTINGS, or replace one in COMMAND-LINE OPTIONS.

### Checking that an option took effect

Most options log nothing when set, and **a misspelt name is ignored without a message**
everywhere, so copy names from the tables and check the effect itself. The game's log (below)
shows some:

- an option the game sets to its own value at start logs `Built-in default: <name> = <value>`;
  when your value is in effect, that line is missing for it;
- a GRAPHICS page setting given on the command line logs
  `Graphics settings: <name> comes from the command line`, and its row says
  "Set by a launch option for this session."

### Logs and crash reports

| | Launcher | Manual install |
|---|---|---|
| Game log | `data\logs\runtime.log` (LOGS opens the folder) | `.local\state\logs\runtime.log` |
| Launcher log | `data\logs\launcher.log` | - |
| Setup log | `data\source\<version>\.local\logs` | `.local\logs` (`setup-error.json`) |
| Crash report | `data\reports` | - |

`run.ps1` starts `runtime.log` afresh at each launch; the launcher appends to it, so the
newest launch is at its end. Past 5 MB the older lines move to `runtime.1.log`,
`runtime.2.log` and so on beside it. When the game stops abnormally, the launcher offers
REPORT CRASH: it packs the end of that launch's log, with folder paths, your Windows user name
and gamertag removed, into a ZIP in `data\reports` (REPORT FOLDER opens it) and opens a
prefilled GitHub issue to attach it to (see [Reporting issues](reporting-issues.md)).
