# Linux port: how it was done and what broke

A summary for reviewers of the native Linux port: the approach, every bug found on the way with
its cause and fix, and what is still untested. Setup and usage are in [guide/linux.md](../guide/linux.md).

**Tested on**

| Machine | CPU / GPU | RAM | OS |
|---|---|---|---|
| Desktop | Ryzen 5 5600G, Radeon RX 6750 XT (RADV) | 32 GB | Fedora 44, KDE Plasma (Wayland) |
| Laptop | Core i3-1215U, Intel UHD graphics (Mesa 26.2) | 16 GB | Fedora 45, KDE Plasma (Wayland) |

Result on both: builds, boots, menus (controller and keyboard), full races and career events, saves
that survive a restart, and a full install from scratch through the launcher, including Disc 2.
Not compared side by side with the Windows build on the same hardware.

## How this was made

The port was done with AI coding assistance, over four days (2026-11-12 to 2026-11-15; the game
itself only ran from the day the disc was dumped). Claude Code (Anthropic) wrote and debugged the
code and this write-up; a small local model (Qwen 3.5 9B in LM Studio) drafted file explanations
and a few small scripts, which were always reviewed and tested before use. I directed the work and
did everything that needed hands and eyes: dumping the discs, playing every test build, listening
for the audio bug, and the clean install on a second machine. Every fix below was checked on real
hardware, not just compiled.

## Approach

1. **Survey first.** Grepped the repo for Windows markers (about 490 hits in 98 files) and went
   through them file by file to decide what actually matters on Linux.
2. **Follow the Mac path, not PowerShell.** `tools/setup-macos.py` already builds the game without
   Windows tools, so Linux got the same kind of script (`linux/setup-linux.py`) with system clang,
   CMake, Ninja and Vulkan. It imports upstream's platform-neutral helpers (`tools/mac_sdk.py`,
   `source_payload()` from the Mac packager) instead of copying them.
3. **Keep upstream edits small.** Linux code lives in `linux/` or new files. Where an upstream file
   had to change, it is a small platform branch (`#if defined(__linux__)`, `!OperatingSystem.IsWindows()`).
4. **A milestone ladder.** Builds → boots to title → menus → finishes a race → saves and reloads.
   Nothing was called fixed without passing a rung.
5. **A clean-machine test.** The launcher package was installed on a laptop that had never seen the
   project. That found three bugs the development PC could not show (see 8-10 below).

## Bugs found and how they were fixed

| # | Symptom | Cause | Fix |
|---|---|---|---|
| 1 | The game refused to start: "default.xex cannot be read" | `FileSha256()` in `src/code_image_check.cpp` only had Windows (BCrypt) and macOS (CommonCrypto) code | A small header-only SHA-256 (`linux/sha256.h`), checked against the standard test vectors and `sha256sum`, behind a `__linux__` branch |
| 2 | Build error: RenderDoc header not found | The game links the SDK's RenderDoc header target only on Windows; the Mac code skips the header, Linux includes it | `setup-linux.py` adds the header folder as a standard include directory (no CMake edit) |
| 3 | Music and effects sounded like robotic static; voice-over was fine | SDL3's native PipeWire backend mangles the game's 6-channel stream. Its PulseAudio backend (still PipeWire underneath, via pipewire-pulse) downmixes correctly | The game sets SDL's audio-driver hint to `pulseaudio,pipewire,alsa` on Linux (`src/fm4_app.h`, `OnPreSetup`). A user's own `SDL_AUDIO_DRIVER` still wins |
| 4 | Occasional crash on a race loading screen (3 of about 4 loads at worst) | In the SDK, `Fiber::Destroy()` for a thread's main fiber sets the *calling* thread's `tls_current_` to null. `~XThread` can run on whichever thread drops the last reference, so it wiped another thread's current fiber; that thread's next `SwitchToFiber` saved into a null pointer. Race loading starts and ends threads, hence the loading screens. Windows doesn't crash because its `SwitchTo` never reads `tls_current_` | Only clear `tls_current_` if it is this fiber (`linux/patches/sdk-0001-...patch`, applied by `setup-linux.py`). Confirmed: the case happened about 50 times in a 35-minute session with no crash. Still present in SDK `d16189ff`, so worth fixing upstream in ShiftGlue |
| 5 | The launcher did Windows things on Linux (PowerShell setup, `.exe`, `win-amd64` folder) | Every platform branch was `IsMacOS() ? mac : windows` | `!IsWindows()` where Linux should behave like the Mac, and a Linux value where it needs one (build folder, executable, setup script, `xdg-open`, protected folders, `linux-x64` runtime) |
| 6 | The launcher couldn't drive the Linux build | It calls the setup script once with `--game-root/--cache-root/--jobs` and reads `::fm4::` progress lines; the Linux script built in separate stages | A full-build mode with the same progress events and `setup-error.json` as the Mac script |
| 7 | Rev 1 Europe discs would lose the game's fonts and shader cache on Linux (the one launcher test that failed on Linux) | That disc spells its folder `MEDIA`. Windows and macOS ignore case; Linux kept `MEDIA` while the game's own code opens `Media/UI` and `Media/Shaders` | The disc installer writes folders with the manifest's spelling (`Disc1Installer.cs`). No effect on case-insensitive systems |
| 8 | The launcher drew its window frame and crashed on Fedora 45: "No usable version of libssl was found" | Fedora 45 ships OpenSSL 4; the launcher's built-in .NET 8 only knows OpenSSL 3 and older | Documented: install OpenSSL 3 alongside (`openssl3-libs`). `check-system.py` detects it. A newer .NET may remove the need (not verified) |
| 9 | Install failed at "Configuring the game build" when the launcher's folder name had a space | The RenderDoc include path (bug 2) was pasted unquoted into `CMAKE_CXX_FLAGS` | Passed as `CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES`, which CMake quotes |
| 10 | The robotic audio came back when the game was started from the launcher | Fix 3 first lived in the command-line run script, which the launcher doesn't use. Setting the environment variable from the game was too late: SDL copies the environment when the window opens | Moved into the game as an SDL hint (see 3) |
| 11 | The FRAME RATE slider wasn't capped to the monitor | `DisplayRefreshHz()` returns 0 off Windows (the SDK has no native window handle there) | On Linux, ask SDL for the display under the game's window (`src/ui/graphics_page.cpp`). Shows 120 on a 120 Hz monitor. Would likely work on macOS too (untested) |
| 12 | Every upstream SDK pin bump broke the next build | CMake refuses a build folder configured for another source, and the game build kept the old `REXSDK_DIR` | `setup-linux.py` starts a build folder fresh when its cached SDK path differs |
| 13 | The launcher's progress bar sat at 50 % for the whole compile (also on macOS) | The setup script only reported stages, nothing during the long compile | `setup-linux.py` reads Ninja's `[done/total]` counter and sends progress during the compile |

## Added for users

- `check-system.py`: checks build packages, OpenSSL 3, a Vulkan GPU and disk space, and prints
  the exact `dnf install` command for anything missing. Setup runs it first, so a missing package
  fails in a second with the fix instead of minutes into the build.
- `add-to-app-menu.sh`: puts the launcher in the desktop's app menu with its icon.
- `linux/package-launcher-linux.py`: builds the Linux launcher package (single-file launcher, source
  payload, the two helpers above).

## Known issues and untested areas

- **Untested:** custom soundtrack folders (case-sensitive paths), updating an existing install,
  Project Forza Plus 4, distributions other than Fedora, NVIDIA GPUs.
- On KDE Plasma (Wayland) the game opens on the monitor under the mouse pointer.
- The last build step (writing the graphics settings list) needs a screen connected; it fails if
  every monitor is unplugged.
- On the test laptop the firmware holds the CPU at about 10 W, so a first build takes about
  25 minutes there (about 6 minutes on the desktop).

## Suggestions for upstream

- The fiber fix (bug 4) belongs in the ShiftGlue SDK; it is a few lines and doesn't change Windows.
- A newer .NET for the launcher may avoid the OpenSSL 3 requirement on new distributions (bug 8).
- The SDL refresh-rate lookup (bug 11) and compile progress (bug 13) would also help macOS.
