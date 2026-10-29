# Manual install

[← Back to the README](../README.md)

The command-line route; the [launcher](launcher.md) does the same for you. Every command is
for PowerShell.

> [!IMPORTANT]
> Folder paths may hold non-English letters and spaces, but **no `[` or `]`**, and no
> parentheses when the path has non-English letters: CMake, which the build uses, cannot run
> from such folders, and setup stops and says so.

## 1. Extract Disc 1

Extract the disc's file system into an empty folder, for example `D:\FM4\disc1`:

- from an ISO, with [extract-xiso](https://github.com/XboxDev/extract-xiso):
  `extract-xiso -x -d D:\FM4\disc1 FM4-Disc1.iso` (the options go before the ISO);
- from a GOD copy, convert it to an ISO first (for example with God2Iso), then extract it.

`default.xex` must sit directly in that folder. Check its hash:

```powershell
Get-FileHash D:\FM4\disc1\default.xex -Algorithm SHA256
```

It must be one of:

| Release | SHA256 |
|---|---|
| 1.0 PAL | `6A7E9AE561CCCA134916F27870A73299408F4AD14F1499063C0F6F6414C2A7E1` |
| 1.0 USA | `3618E85AF83E972DDCC1B0D0DD808E78E29AB4AB3890A15B31C210A6A3E93E09` |

## 2. Get the source

```powershell
git clone https://github.com/i2kiselev/fm4-static-recomp D:\FM4\fm4-static-recomp
```

Or download the repository as a ZIP and extract it there. No submodules are needed.

## 3. Build

```powershell
cd D:\FM4\fm4-static-recomp
powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup.ps1 -GameRoot D:\FM4\disc1 -AcceptMicrosoftLicense
```

Give `-GameRoot` without a trailing backslash.

`-AcceptMicrosoftLicense` accepts the licences of the Microsoft files setup downloads: the
[Visual Studio Build Tools licence](https://go.microsoft.com/fwlink/?LinkId=2179911) (MSVC's C
and C++ runtime headers and libraries) and the
[Windows SDK licence](https://aka.ms/WinSDKLicenseURL). They come straight from Microsoft and
stay on your PC; `tools\microsoft-licences.ps1` prints the current links.

- Without the switch, setup uses an installed Visual Studio or Build Tools that holds the
  14.44 toolset, or stops and says so.
- `-ToolchainSource VisualStudio` uses Microsoft's installer instead (it asks for
  administrator permission once and can take 10 minutes or more).

<details>
<summary>What setup does</summary>

1. Downloads pinned, hash-checked copies of CMake, Ninja, LLVM/clang, xz and Python, and the
   ShiftGlue SDK prebuilt for this release, into `.local`. The LLVM copy holds only the files
   the build uses, taken unmodified from the official LLVM release (`tools\package-llvm.py`
   makes it); the SDK is built from its pinned source by `tools\package-sdk.ps1` and holds no
   game code. Large files download over several connections at once.
2. Downloads Microsoft's C++ runtime (the MSVC 14.44 headers and libraries, from Microsoft's
   Visual Studio channel) and the Windows SDK (from Microsoft's NuGet packages) into
   `.local\toolchain`, each checked against Microsoft's published hash.
3. Translates `default.xex` into C++ and compiles
   `out\build\win-amd64-release\fm4_static_recomp.exe`.
4. Builds the shader cache from your Disc 1 (about a second), so your first races do not
   stall while shaders compile. It adds the shaders and pipelines listed in
   `config\shaders` to your cache and never removes anything; the repository ships only
   that list (hashes, render states, edit positions), no game data. The first launch then
   compiles the listed pipelines for your GPU before the title screen: about 30 seconds on
   the [test bench](performance.md#test-bench), longer on a processor with fewer threads; the
   game may look frozen meanwhile. `-SkipShaderCache` leaves it out. Pass the same `-StateRoot` you play with so the cache
   lands there.

</details>

Nothing outside the repository folder changes. If a step fails, the last lines name it and
`.local\logs\setup-error.json` holds the details. Running the same command again resumes:
finished downloads are not repeated.

## 4. Play

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run.ps1 -GameRoot D:\FM4\disc1
```

Your save, logs and settings go to `.local\state` in the repository (`user` is the save).
`-StateRoot <folder>` puts them elsewhere; each state folder is a separate save, and setup
takes the same `-StateRoot` so the shader cache lands there.

> [!TIP]
> Back up `.local\state\user` before you update or experiment.

[Launch options](launch-options.md) go after the script's own parameters, for example
`... tools\run.ps1 -GameRoot D:\FM4\disc1 --resolution_scale=2`.

## 5. Disc 2 and DLC (optional)

Put the four Disc 2 content packages in one folder (in a dump they sit in a folder named
`00000002`) and your DLC packages in another (subfolders are searched), then launch once
with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run.ps1 -GameRoot D:\FM4\disc1 `
    --fm4_disc2_packages=D:\FM4\disc2 --fm4_dlc_packages=D:\FM4\dlc
```

The packages are installed into your save folder in a few seconds; installed ones are
skipped, so passing the options every time does no harm. `.local\state\logs\runtime.log`
ends the install with a `Content: ...` summary.

## Updating

> [!IMPORTANT]
> Close the game first.

```powershell
git pull
powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup.ps1 -GameRoot D:\FM4\disc1
```

Your save, settings, Disc 2 and DLC stay as they are.
