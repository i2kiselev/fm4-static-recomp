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

2. Start `FM4StaticRecompLauncher.exe`. Its first screen has five steps:

   | Step | What happens |
   |---|---|
   | **Disc 1** | Drop or choose your ISO, GOD folder or extracted folder. It is verified and extracted. |
   | **Disc 2** (optional) | An ISO, a GOD folder or the folder with its four packages, or **Later** (CONTENT installs it any time). It is verified now and installed after the build, so keep it where it is until then. |
   | **Microsoft licence** | The build downloads Microsoft's C++ runtime and Windows SDK files from Microsoft. Read and accept their licences, or decline and use Microsoft's installer instead, which asks for administrator permission once. |
   | **Build** | The launcher downloads its build tools, translates and compiles the game. |
   | **Your profile** | A name; the gamertag starts as it. |

3. **PLAY**. The first start compiles the game's shaders for your GPU before the title screen:
   about 30 seconds, longer on a processor with fewer threads, and the game may look frozen
   meanwhile. Later starts take a second or two; the compile runs again after an update and
   after a GPU driver update.

## The main screen

| Button | What it does |
|---|---|
| **PLAY** | Starts the game. |
| **PROFILE** | Adds, renames, imports and deletes profiles (separate saves). |
| **CONTENT** | Installs Disc 2 and DLC. |
| **SETTINGS** | The [GRAPHICS page's settings](graphics.md), the game's language, launch options, advanced options and [command-line options](launch-options.md#with-the-launcher-command-line-options) for debugging. |

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
builds, your saves and settings. Move or back up the folder as a whole.

- A first setup with Disc 2 takes about **13 GB** there (Disc 1 7.7 GB, Disc 2 2.8 GB, the
  build tools 1.4 GB, and the game's source and build 1 GB), and setup asks for a 2 GB margin
  on top while it works.
- An update builds the new version beside the old one, about 1 GB more until the older one
  is removed.

> [!IMPORTANT]
> The folder's limits come from the build tools: the path of `data\` may be at most 54
> characters, may not hold `[` or `]`, and may not hold parentheses together with
> non-English letters. The launcher checks this at start and names the folder if it cannot
> use it.

## Updates

The launcher checks GitHub when it starts (and from CHECK FOR UPDATES).

1. **UPDATE TO** downloads the new release and restarts.
2. The new version then builds beside the current one, which you can keep playing.
3. **GO BACK TO** switches back.

Saves, content and settings are kept; each profile's save is zipped to `data\backups\`
before a new version first runs it.
