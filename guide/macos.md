# Experimental macOS support

[← Back to the README](../README.md)

FM4 Static Recomp has an experimental preview for Apple Silicon Macs. As on Windows, the game
is built on your Mac from your own disc; it draws through Vulkan on MoltenVK. No game data or
game program is distributed.

## What you need

- A Mac with Apple Silicon (M1 or newer) and macOS 14 or newer (so far tested on newer
  versions only).
- Your own [supported FM4 Disc 1](../README.md#supported-game); Disc 2 is optional.
- An internet connection during setup, and about 10 GB free where the game data goes (see
  [Where the game data goes](#where-the-game-data-goes)).

## Installing

1. Install the build tools. Open **Terminal** (in Applications → Utilities) and run:

   ```sh
   xcode-select --install
   ```

   Confirm the window that opens and wait until it finishes.
2. Install [Homebrew](https://brew.sh): copy the one command shown on its page into Terminal
   and follow what it says. Then run:

   ```sh
   brew install python cmake ninja
   ```

3. Download `FM4StaticRecomp-Launcher-macos-arm64.zip` from the
   [latest release](https://github.com/i2kiselev/fm4-static-recomp/releases/latest) and
   double-click it to extract `FM4 Static Recomp.app`.
4. Move `FM4 Static Recomp.app` to **Applications** and open it. If macOS says the app can't be
   opened, open **System Settings → Privacy & Security**, click **Open Anyway** next to
   FM4 Static Recomp, and open the app again.
5. Follow the setup steps, the same as on Windows ([First setup](launcher.md#first-setup)):
   - **DISC 1:** **CHOOSE DISC IMAGE** for an ISO, or **CHOOSE FOLDER** for a GOD folder or an
     extracted folder. A Finder alias to it works too.
   - **DISC 2:** **CHOOSE DISC 2** or **CHOOSE DISC 2 FOLDER**, or **LATER**.
   - **YOUR PROFILE:** a name, or **CHOOSE EXISTING SAVE…** to continue a career you already
     have ([Existing saves](#existing-saves)).

   Setup verifies the disc, downloads the SDK and builds the game; the originals you chose stay
   where they are.
6. Press **PLAY**. Unlike on Windows, the first start reaches the title in seconds: on a Mac,
   shaders compile while you play, so the first races can stutter briefly. The game saves the
   shaders it compiled, and later starts prepare them before the title with a progress bar.

Start with vanilla FM4 and a 60 FPS limit. Project Forza Plus 4 can be chosen at setup, but it
has not been tested on a Mac yet.

## Where the game data goes

The game data (discs, content, profiles, settings and the build) goes to
`~/Library/Application Support/FM4 Static Recomp/data`. At the first start the launcher shows
this folder. **USE THIS FOLDER** keeps it, and **CHOOSE DATA FOLDER…** lets you pick a
different folder, including one on an external drive. An app that is not in Applications
keeps its `data` folder next to it instead.

The external volume must be writable on macOS and support creating and following
directory symbolic links. APFS and Mac OS Extended support these features natively.
Other formats can work when their macOS driver provides both. NTFS was tested
successfully with a compatible third-party driver. macOS's built-in NTFS support
provides read access only; see [Apple's guidance on external drives](https://support.apple.com/en-gb/101830).

The game's logs are in the data folder's `logs` folder.

## Updating

The launcher does not update itself on a Mac. To update:

1. Close the game and the launcher.
2. Download the new `FM4StaticRecomp-Launcher-macos-arm64.zip` from the
   [latest release](https://github.com/i2kiselev/fm4-static-recomp/releases/latest) and
   double-click it.
3. Replace `FM4 Static Recomp.app` in Applications with the new one and open it. It keeps your
   data folder and builds the new version.

## Settings and keys

The launcher's pages are the same as on Windows ([Launcher](launcher.md)). SETTINGS has
CONTROLLER VIBRATION and, right after it, VIBRATION STRENGTH (0–200 %): turning CONTROLLER
VIBRATION off turns all vibration off and keeps the strength for later. SETTINGS → ADVANCED has
SHOW FPS (F3 shows and hides it while you play) and AUDIO POP FIX, as on Windows.

The keys are in [Keyboard](controls.md). On a Mac keyboard, **Option+Enter** works like
Alt+Enter and switches between a window and fullscreen. F6 opens the in-game GRAPHICS and
KEYBOARD pages. [Mac upscaling](macos-upscaling.md) says which upscalers work; MetalFX and frame
generation are not part of this preview.

## Existing saves

**CHOOSE EXISTING SAVE…** at setup, or **PROFILE → IMPORT SAVE** later, copies a career you
already have. Choose the save's Xbox profile folder, the `user` folder that holds it, or a
Finder alias to either. If the folder holds several careers, the launcher lists them to pick
one. The import is a copy: your original save stays as it is. **NEW CAREER** cancels a save
chosen at setup.

## If geometry or textures look wrong

The preview uses two Mac-only defaults against problems testers saw: IEEE vertex math (against
a roof clipping through the Aston Martin DB9) and a GPU memory check before every 64th draw
instead of every draw (against stutter in races). If geometry or textures look wrong, open the
launcher's SETTINGS → ADVANCED, enter `--unprotected_page_check_interval=1` in COMMAND-LINE
OPTIONS, press **SAVE** and start the game again: it then checks before every draw, which may
cost some speed. Remove the option to go back to the Mac default.

For developers, the background: `spirv_ieee_vertex_math` is on by default on Mac after a
tester repeatedly saw the DB9 roof clip with it off ([report](https://github.com/i2kiselev/fm4-static-recomp/pull/47#issuecomment-6707870666));
`unprotected_page_check_interval=64` follows an M1 Max tester's trace of race-time spikes to the
per-draw memory comparisons ([experiment](https://github.com/i2kiselev/shiftglue-sdk/pull/4#issuecomment-6710898407)).
A later check can leave geometry or textures stale until the next check; checks made when
memory ranges are requested still run. A more precise memory-tracking fix is planned.
`--spirv_ieee_vertex_math=false` turns the vertex-math default off for a comparison.

## Build from source

```sh
git clone https://github.com/i2kiselev/fm4-static-recomp.git
cd fm4-static-recomp
git checkout v0.3.3
# Put your extracted Disc 1 at game/disc1, then:
./tools/build-mac.sh
out/build/mac-arm64-release/fm4_static_recomp \
  --game_data_root="$PWD/game/disc1" --user_data_root="$PWD/user" --gpu_backend=vulkan \
  --fm4_skip_intro=true
```

`FM4_GAME_ROOT=/absolute/path/to/disc1 ./tools/build-mac.sh` selects another disc
folder. Use a separate user directory when comparing builds.

Mac setup uses the SDK revision pinned in `config/release-toolchain.json`, as
Windows does. No local patch stack is applied.

Setup keeps managed SDK source under `.local/shiftglue-sdk-macos/<revision>` and
builds the complete host, facade modules and FidelityFX runtime in
`out/build/mac-arm64-release`. Build receipts are under `.local/logs`. A developer
may explicitly set `FM4_SDK_ROOT` (read by `tools/setup-macos.py`) to a checkout at the
selected revision; extra working changes are recorded. This override does not qualify a clean install.

After a successful build, `.local/logs/macos-build.json` in the source folder
records the SDK identity, compiler and tool versions, code image, and
binary hashes. `.local/logs/macos-sdk.json` records SDK preparation. These receipts
describe the build inputs and outputs; they do not assert that gameplay has been
validated or that different Xcode/tool versions produce byte-identical binaries.

The clean-source macOS build has also been checked with the declared macOS 14
compile target. Control-rate parsing uses a C-locale compatibility path on Apple
platforms because current libc++ marks floating-point `from_chars` as macOS 26+.
The native controls regressions and FSR GPU checks run on macOS 27; running on
macOS 14 itself still needs validation.

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

Without `--output-dir` the app is written to `out/launcher-macos`; start it with
`open "out/launcher-macos/FM4 Static Recomp.app"`.

Packaging runs unattended, includes only allowlisted source/launcher files and
notices, verifies the app signature, and writes the ZIP and SHA-256 sidecar.
It does not include development SDK hints, disc data, generated guest code, saves
or caches. The `macos-ci` pull-request label enables automated launcher, Python,
native and SDK checks on an Apple Silicon macOS 15 runner, targeting macOS 14.
Tagged builds run the same checks and retain the ZIP and checksum as Actions
artifacts; they do not publish a GitHub release. The tag must match the release
version and use the shared SDK pin. The game
is still built on each player's Mac.

## Testing status

Contributors tested the preview on Apple M1 Max and M3 Macs: setup from the app and Free Play
races in vanilla FM4 at a 60 FPS limit, and a run with Vulkan validation that logged no errors.
A fresh setup from the app package, with the game data on an external NTFS drive (third-party
driver), reached the title in about 9 seconds and played a race smoothly, with a few dips while
shaders compiled. That setup hit an Apple Clang 21 crash in precompiled headers; setup now turns
them off on a Mac. The automated checks cover neighbouring 4 KB guest-page writes on a 16 KB
host page and FSR 2/3 on the GPU. Not tested yet: a full setup with Apple Clang 21 since that
change, macOS 14 itself, Project Forza Plus 4, and FSR 2/3 image quality in races.
