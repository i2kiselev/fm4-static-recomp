# Experimental Linux support

[← Back to the README](../README.md)

FM4 Static Recomp has an experimental preview for Linux x86-64. As on Windows and macOS, the
game is built locally from your own disc; no game files are distributed. Other distributions than
Fedora are untested.

## What you need

- A Linux system with a Vulkan GPU driver (check with: `vulkaninfo --summary`).
- Your own [supported FM4 Disc 1](../README.md#supported-game). Disc 2 (optional, more cars and
  tracks) installs through the launcher as on Windows.
- About 7 GB of free RAM at peak while compiling, and these packages (Arch-based distributions
  such as Manjaro and EndeavourOS use the Arch line):

  **Fedora:**

  ```sh
  sudo dnf install python3 git clang lld cmake ninja-build vulkan-loader-devel vulkan-headers vulkan-tools libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel wayland-devel libxkbcommon-devel mesa-libGL-devel libXScrnSaver-devel libXfixes-devel libXtst-devel alsa-lib-devel pulseaudio-libs-devel pipewire-devel pipewire-jack-audio-connection-kit-devel dbus-devel ibus-devel systemd-devel mesa-libEGL-devel mesa-libGLES-devel mesa-libgbm-devel libdrm-devel wayland-protocols-devel libdecor-devel libusb1-devel
  ```

  **Arch:**

  ```sh
  sudo pacman -S --needed python git clang lld cmake ninja pkgconf vulkan-icd-loader vulkan-headers vulkan-tools vulkan-radeon vulkan-intel libx11 libxext libxrandr libxcursor libxi wayland libxkbcommon mesa libxss libxfixes libxtst alsa-lib libpulse pipewire pipewire-jack dbus ibus systemd libdrm wayland-protocols libdecor libusb
  ```

  (`vulkan-radeon` is the AMD graphics driver and `vulkan-intel` the Intel one; you only need the one for your GPU.)

  **Ubuntu** (and Ubuntu-based distributions such as Linux Mint and Pop!_OS):

  ```sh
  sudo apt install python3 git clang lld cmake ninja-build pkg-config libvulkan-dev vulkan-tools mesa-vulkan-drivers libx11-dev libx11-xcb-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libwayland-dev libxkbcommon-dev libgl-dev libxss-dev libxfixes-dev libxtst-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libjack-jackd2-dev libdbus-1-dev libibus-1.0-dev libudev-dev libegl-dev libgles-dev libgbm-dev libdrm-dev wayland-protocols libdecor-0-dev libusb-1.0-0-dev
  ```

  Ubuntu 24.04's default clang (18) is too old for this code, so on 24.04 also run
  `sudo apt install clang-20`; the build uses it automatically.

- The launcher has .NET built in but needs OpenSSL 3. Fedora 45 and Arch ship OpenSSL 4, so also run:

  **Fedora:** `sudo dnf install 'libssl.so.3()(64bit)'` (installs openssl3-libs on Fedora 45; already there on Fedora 44)

  **Arch:** `sudo pacman -S --needed openssl-3`

  **Ubuntu:** `sudo apt install libssl3t64` (normally already installed)

## Installing

1. Download `FM4StaticRecomp-Launcher-linux-x64.tar.gz` and unpack it in a normal folder in your
   home directory (not `/tmp`: the launcher refuses temporary folders):

   ```sh
   tar -xzf FM4StaticRecomp-Launcher-linux-x64.tar.gz
   cd FM4StaticRecomp-Launcher-linux-x64
   python3 check-system.py
   ```

   `check-system.py` checks the build packages, OpenSSL 3, the Vulkan GPU and disk space, and
   prints the install command for anything missing (`dnf` on Fedora, `pacman` on Arch, `apt` on Ubuntu). Setup runs the same check before it builds.
2. Optionally, `./add-to-app-menu.sh` puts the launcher in your desktop's app menu (run it again
   after moving the folder; `--remove` takes it out). Start the launcher from the menu or with
   `./FM4StaticRecompLauncher`.
3. Point the launcher at your Disc 1 (an extracted disc folder or an `.iso`) and let it install.
   Setup downloads and builds the pinned ShiftGlue SDK, then compiles the game. First build: about
   6 minutes on a desktop Ryzen-class CPU, about 13 minutes on a Core i3-1215U laptop.
4. Press **PLAY**. The progress bar follows the compile file by file.

Project Forza Plus 4 has not been tested on Linux yet.

## Flatpak (Steam Deck, Bazzite and other distributions)

The Flatpak brings everything the build needs (clang, CMake, Ninja, Vulkan and OpenSSL), so you
install no packages. It is meant for systems where installing them is hard, such as SteamOS and
Bazzite. Only `flatpak` itself is needed (preinstalled on SteamOS, Bazzite and most desktop
distributions).

1. Download `FM4StaticRecomp.flatpak` and install it (on a Steam Deck, in Desktop Mode):

   ```sh
   flatpak install --user FM4StaticRecomp.flatpak
   ```

   The first install also downloads the Freedesktop SDK runtime from Flathub (about 600 MB, shared
   with other Flatpak apps).
2. Start **FM4 Static Recomp** from the app menu (or `flatpak run io.github.waterbiscuit5.FM4StaticRecomp`),
   point it at your Disc 1 and let it install, as above. The first build also downloads the
   ShiftGlue SDK (about 200 MB); the download step shows no progress bar.

The Flatpak can read your home folder and removable drives (`/run/media`, `/media`, `/mnt`) to
find your discs; it cannot change them. To remove it with all its data:
`flatpak uninstall --user --delete-data io.github.waterbiscuit5.FM4StaticRecomp`.

Tested on Fedora 44 only. Untested on the Steam Deck so far, including running it from Game Mode.

## Where the game data goes

The launcher keeps the game data (the build, settings and profiles) in a `data` folder beside the
launcher. The Flatpak keeps it in `~/.var/app/io.github.waterbiscuit5.FM4StaticRecomp/data/data`.

## Updating

Manual for the Linux preview: the launcher does not update itself on Linux. Keep the `data`
folder beside the launcher when you replace it with a newer package. (Updating has not been
tested on Linux yet.)

## Settings and keys

See [Launcher](launcher.md) for the launcher's pages and [Keyboard](controls.md) for the keys.
Menus work with a controller and with the keyboard.

## Known issues

- On KDE Plasma Wayland the game opens on the monitor under the mouse pointer; keep the pointer on the screen you want.
- The graphics page cannot read the display refresh rate on Linux, so the FRAME RATE slider is not capped to your monitor (same as macOS).
- Crash reports and the Microsoft licence screen are Windows-only and are skipped.

## Audio

On Linux the game picks SDL's PulseAudio backend (works through PipeWire's pipewire-pulse), because SDL's native PipeWire backend gives robotic static with the game's 6-channel audio. Override with the environment variable `SDL_AUDIO_DRIVER`.

## Build from source

```sh
python3 linux/setup-linux.py --stage sdk
python3 linux/setup-linux.py --stage game --game-root /path/to/disc1
linux/run-linux.sh --game-root /path/to/disc1
```

Unit tests:

```sh
python3 linux/setup-linux.py --stage tests [--game-root /path/to/disc1]
```

Launcher: needs Fedora's dotnet-sdk-8.0 (`sudo dnf install --repo=fedora --repo=updates dotnet-sdk-8.0`); `dotnet test launcher/Fm4StaticRecomp.Launcher.sln`.

Package:

```sh
python3 linux/package-launcher-linux.py
```

It writes `out/launcher-linux/FM4StaticRecomp-Launcher-linux-x64.tar.gz`.

The ShiftGlue SDK gets one Linux patch (`linux/patches/`): a fix for a crash when loading races (a thread's fiber state was cleared from another thread). `setup-linux.py` applies it automatically.

## Testing status

Tested on Fedora 44 (AMD Radeon RX 6750 XT, RADV driver, KDE Plasma Wayland) and Fedora 45 (Intel Alder Lake laptop graphics, Mesa 26.2.6, 16 GB RAM): builds; boots to the title screen; menus; finished races and career events; saves and reloads; full install through the launcher on a clean Fedora 45 laptop. Not yet compared with the Windows build on the same hardware.
