# Launcher

[← Back to the README](../README.md)

The launcher sets everything up from your discs and starts the game. For the command line
instead, see the [manual install](manual-install.md).

## First setup

1. Download `FM4StaticRecomp-Launcher.zip` from the
   [releases](https://github.com/i2kiselev/fm4-static-recomp/releases) and extract it into a
   short folder you own, for example `D:\FM4`.

   > [!WARNING]
   > Not into `Program Files`, and not by starting the launcher from inside the zip's window:
   > Windows runs it from a temporary folder then.

2. Start `FM4StaticRecompLauncher.exe`. Its first screen has these steps:

   | Step | What happens |
   |---|---|
   | **Game** | Vanilla, or [Project Forza Plus 4](project-forza-plus.md) 1.3 over it. Chosen once: the install cannot be switched later. For the mod next to a vanilla install, extract the launcher into a second, empty folder. |
   | **Disc 1** | Drop or choose your ISO, GOD folder or extracted folder. It is verified and extracted. |
   | **Project Forza Plus 4 1.3** (mod installs only) | The mod's zip or folder, in place of the Disc 2 step; see [Project Forza Plus 4](project-forza-plus.md#setup). |
   | **Disc 2** (optional) | An ISO, a GOD folder or the folder with its four packages, or **Later** (CONTENT installs it any time). It is verified now and installed after the build, so keep it where it is until then. |
   | **Microsoft licence** | The build downloads Microsoft's C++ runtime and Windows SDK files from Microsoft. Read and accept their licences, or decline and use Microsoft's installer instead, which asks for administrator permission once. |
   | **Build** | The launcher downloads its build tools, translates and compiles the game. |
   | **Your profile** | A name; the gamertag starts as it. **CHOOSE EXISTING SAVE…** starts the profile from a save you already have ([Importing saves](saves.md#importing-saves)); **NEW CAREER** undoes that. |

3. **PLAY**. The first start prepares shaders for your GPU before the title screen. A progress
   bar shows completed work, and the window stays responsive. The time depends on your computer
   and graphics driver. Completed work is saved every 10 seconds and at the end, making later
   starts much faster. An update or a GPU driver update may require preparation again. Closing
   during preparation may briefly wait for the current graphics operation to finish.

## The main screen

| Button | What it does |
|---|---|
| **PLAY** | Starts the game. |
| **PROFILE** | Adds, renames, imports and deletes [profiles](saves.md) (separate saves). |
| **CONTENT** | Installs Disc 2 and DLC. Install them only here: the game's own Disc 2 install waits for a disc change that never comes. Also [unlocks the unicorn cars](#unicorn-cars). |
| **GRAPHICS** | [Playing in a window](windowed.md) and the [GRAPHICS page's settings](graphics.md). **SAVE** on top writes this page and SETTINGS together. |
| **SETTINGS** | The game's language, launch options (among them CONTROLLER VIBRATION, and VIBRATION STRENGTH, 0–200 %, for a controller that vibrates too weakly), [CUSTOM SOUNDTRACK](launch-options.md#custom-soundtrack) for your own music, advanced options (among them AUDIO POP FIX, on by default, which smooths the pops in the engine sounds, [AUDIO FOR OLDER CPUS](troubleshooting.md#sound-is-slow-late-or-crackles-on-an-older-cpu), off by default, MUSIC ON CRASH and MUSIC FILTER for the race music, SHOW FPS, the [saves folder](saves.md#the-saves-folder), [PRESENT THROUGH DXGI](launch-options.md#frame-generation) and [30 FPS (DIAGNOSTIC)](high-frame-rate.md#testing-a-slow-processor)) and [command-line options](launch-options.md#with-the-launcher-command-line-options) for debugging. On a Project Forza Plus 4 install also its [career mode](project-forza-plus.md#career-modes-and-harder-ai). |
| **CONTROLS** | The keyboard's keys and how fast steering and pedals ramp: [Rebinding keys](controls.md#rebinding-keys). Has its own **SAVE**. |

Installed DLC is treated as fully unlocked. The game automatically repairs incomplete license
information on startup. Existing installs do not need the original packages again for this
repair; car files and saves stay unchanged.

## Playing in a window

**WINDOWED** and **WINDOW SIZE**, at the top of GRAPHICS, start the game in a window, and
**Alt+Enter** switches between a window and fullscreen while you play. Resizing, black bars and
upscalers in a window: [Playing in a window](windowed.md).

## Profiles and saves

Each profile is a separate save. **PROFILE** adds, renames, imports and deletes them; **IMPORT
SAVE** also takes a career from Xenia Canary, and SETTINGS → ADVANCED → **SAVES FOLDER** keeps the
saves outside `data\`. All of it: [Profiles and saves](saves.md).

## Unicorn cars

Unicorn cars were rare gifts on Xbox Live; the game keeps them out of the Autoshow. With the game
closed, CONTENT → **UNLOCK UNICORN CARS** makes the 17 of the original disc buyable for every
profile of the install (some are on Disc 2, so install that too). It changes the game's database
and keeps the original as `data\game\Media\db\gamedb.before-unicorns.slt`. Rev 1 discs and
Project Forza Plus 4 have them unlocked already; their Warthog and Bentley 8 Litre stay locked,
since the game has only their Autovista model. An update that extracts Disc 1 again locks the
cars again: unlock them once more.

## Starting from Steam, Pegasus or another frontend

`FM4StaticRecompLauncher.exe --skip-launcher` starts the game without showing the launcher, on
the profile last chosen in it; `--profile "<name>"` picks another profile by its name. The
launcher waits in the background while the game runs and closes with it, so the frontend sees
the game running for its whole length. It does not check for updates then: start it as usual
for those.

Its window opens when you are needed: while setup or a new version's build is unfinished, for
an unknown profile name, when the game does not start, and after a crash (REPORT CRASH).

- **Steam:** Games → Add a Non-Steam Game to My Library, choose
  `FM4StaticRecompLauncher.exe`, then in the shortcut's Properties set Launch Options to
  `--skip-launcher`.
- **Pegasus, Playnite, LaunchBox and others:** the command
  `"D:\FM4\FM4StaticRecompLauncher.exe" --skip-launcher`, with your folder.

## The `data` folder

Everything lives in `data\` beside the launcher: the extracted disc, the build tools, the
builds, your saves and settings. Move or back up the folder as a whole, or keep the saves in a
[folder of your own](saves.md#the-saves-folder).

- A first setup with Disc 2 takes about **13 GB** there (Disc 1 7.7 GB, Disc 2 2.8 GB, the
  build tools 1.4 GB, and the game's source and build 1 GB), and setup asks for a 2 GB margin
  on top while it works. A [Project Forza Plus 4](project-forza-plus.md) install takes about
  **20 GB**, with the mod's content in place of Disc 2's.
- An update builds the new version beside the old one, about 1 GB more until the older one
  is removed.

> [!IMPORTANT]
> The folder's limits come from the build tools: the path of `data\` may be at most 91
> characters, may not hold `[` or `]`, and may not hold parentheses together with
> non-English letters. The launcher checks this at start and names the folder if it cannot
> use it.

## Updates

The launcher checks GitHub when it starts (and from CHECK FOR UPDATES).

1. **UPDATE TO** downloads the new release and restarts.
2. The new version then builds beside the current one, which you can keep playing.
3. **GO BACK TO** switches back.

Saves, content and settings are kept; each profile's save is zipped to `backups\` in the saves
folder (`data\backups\` by default) before a new version first runs it.

## On a Mac

The launcher also runs as an app on Apple Silicon Macs, as an experimental preview. Setup works as on Windows; where the game data goes, updates and the known limits are in the [macOS guide](macos.md).
