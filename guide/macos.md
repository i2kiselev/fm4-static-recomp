# Experimental macOS support

[← Back to the README](../README.md)

FM4 Static Recomp supports an experimental Apple Silicon build using ShiftGlue’s
Vulkan renderer through MoltenVK. The game is built locally from your own verified
disc. No game data or game executable is distributed.

## Requirements and installation

- Apple Silicon Mac; the build targets macOS 14.0 or newer. Runtime qualification
  on the minimum OS remains pending.
- Xcode Command Line Tools, Python 3.11+, CMake 3.25+ and Ninja.
- Your own [supported FM4 Disc 1](../README.md#supported-game); Disc 2 is optional.
- Internet access for the pinned SDK and build dependencies, and a writable
  installation folder with space for the discs, SDK sources and build outputs.

Install the build tools from a native ARM64 terminal:

```sh
xcode-select --install
brew install python cmake ninja
```

When a macOS preview is published, download
`FM4StaticRecomp-Launcher-macos-arm64.zip` and its `.sha256` sidecar from the
[project releases](https://github.com/i2kiselev/fm4-static-recomp/releases).
Verify the archive, extract it into a writable folder you own, and open
`FM4 Static Recomp.app`:

```sh
shasum -a 256 -c FM4StaticRecomp-Launcher-macos-arm64.zip.sha256
```

Follow the launcher’s setup steps with your own discs, then press PLAY. The
launcher fetches the pinned SDK, translates the supported disc’s code and compiles
the game. First play compiles shaders; revisit a race to compare warm-cache behavior.
Use vanilla FM4 and a 60 FPS limit for the initial test. Project Forza Plus 4’s
managed installer is available, but its Mac setup/gameplay path is not qualified.

Keep the adjacent `data` folder when updating: it holds discs, content, profiles,
settings, source and builds. Back up profiles before replacing an installation.
Mac updates are manual. Preview bundles are ad-hoc signed, not notarized;
Gatekeeper may require approval in System Settings → Privacy & Security.

## Settings and controls

The launcher’s GRAPHICS page controls resolution, upscaling and the initial
window size. SETTINGS includes SHOW FPS, CONTROLLER VIBRATION and VIBRATION
STRENGTH (0–200%). Turning CONTROLLER VIBRATION off forces all rumble off while
retaining the saved strength for later. AUDIO POP FIX is available as in Windows.

Keyboard/controller mappings are in [Keyboard](controls.md). On a Mac keyboard,
Option+Enter corresponds to Alt+Enter for switching fullscreen. F6 opens the
in-game settings with GRAPHICS and KEYBOARD tabs. [Mac upscaling](macos-upscaling.md) describes FSR support and
its test scope. MetalFX and frame interpolation are not part of this preview.

## Build from source

```sh
git clone https://github.com/i2kiselev/fm4-static-recomp.git
cd fm4-static-recomp
git checkout release/0.3.2
# Put your extracted Disc 1 at game/disc1, then:
./tools/build-mac.sh
out/build/mac-arm64-release/fm4_static_recomp \
  --game_data_root="$PWD/game/disc1" --user_data_root="$PWD/user" --gpu_backend=vulkan
```

`FM4_GAME_ROOT=/absolute/path/to/disc1 ./tools/build-mac.sh` selects another disc
folder. Use a separate user directory when comparing builds.

SDK changes for this preview are being reviewed in
[shiftglue-sdk#4](https://github.com/i2kiselev/shiftglue-sdk/pull/4). During review,
`config/macos-toolchain.json` temporarily selects that candidate SDK commit.
After it merges and the maintainer sets the release SDK pin, remove the temporary
`sdk` entry: Mac setup then takes its repository/revision from
`config/release-toolchain.json`, as Windows does. Only Mac-specific configuration
such as `minimum_macos` remains in the Mac file. No local patch stack is applied.

Setup keeps managed SDK source under `.local/shiftglue-sdk-macos/<revision>` and
builds the complete host, facade modules and FidelityFX runtime in
`out/build/mac-arm64-release`. Build receipts are under `.local/logs`. A developer
may explicitly set `FM4_SDK_ROOT` to a checkout at the selected revision; extra
working changes are recorded. This override does not qualify a clean install.

## Tests and packaging

From a configured source build:

```sh
dotnet test launcher/Fm4StaticRecomp.Launcher.Tests -c Release
python3 -m unittest discover -s tools/tests
cmake -S . -B out/build/mac-arm64-release -DFM4_BUILD_TESTS=ON -DFM4_BUILD_MACOS_MEMORY_TESTS=ON
cmake --build out/build/mac-arm64-release --target fm4_unit_tests fm4_macos_page_watch_test --parallel 4
FM4_TEST_GAME_ROOT="$PWD/game/disc1" out/build/mac-arm64-release/fm4-tests/fm4_unit_tests
out/build/mac-arm64-release/fm4_macos_page_watch_test
python3 tools/test-macos-upscalers.py --build-dir out/build/mac-arm64-release
```

On a clean CI checkout, configure with `-DFM4_TESTS_ONLY=ON` and
`-DREXSDK_DIR=/path/to/the/pinned/sdk` to build `fm4_unit_tests` and
`fm4_macos_page_watch_test` without discs or generated game code. Use
`-DCMAKE_OSX_ARCHITECTURES=arm64` for the Mac runner. Keep
`-DFM4_BUILD_MACOS_MEMORY_TESTS=ON` for the page regression.

Game-dependent native tests skip without local disc fixtures. The page-coherency
test needs no game data or GPU; it skips on hosts without 16 KB pages.

Packaging also requires the .NET 8 SDK and uses `config/release.json` for the
same project repository and version as Windows. For local packaging verification,
use a fresh output directory:

```sh
python3 tools/package-launcher-macos.py --output-dir out/macos-package-check
```

Packaging runs unattended, includes only allowlisted source/launcher files and
notices, verifies the app signature, and writes the ZIP and SHA-256 sidecar.
It does not include development SDK hints, disc data, generated guest code, saves
or caches. Official release assets are intended to be built by the maintainer’s
CI from the release tag; the game is still built on each player’s Mac.

## Testing status

Apple M3 testing reported stable 60 FPS with default graphics and a 60 FPS limit
in menus and a default Free Play race on November 2, 2026. On November 5, a race
using the updated SDK and restored vertex residency shortcut was reported free
of observed geometry issues. This does not qualify every track, camera or mode,
and the validation-enabled run is not a performance benchmark.

SDK/native regression coverage includes real neighboring 4 KB guest-page writes
on a 16 KB host page. FSR 2/3 synthetic GPU dispatch and resize checks passed;
explicit gameplay image-quality qualification remains separate. A current Khronos
validation layer is required for meaningful logs: the installed 1.3.236 layer
reported `03047`, while an otherwise identical startup/title comparison with
1.4.363 logged no Vulkan errors. Post-rework install, race and validation results
must be reported in the PR before merge.
