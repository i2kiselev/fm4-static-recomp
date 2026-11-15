# Common problems

[← Back to the README](../README.md)

Problems players have run into, and what fixes them. Not listed here? See
[Reporting issues](reporting-issues.md).

## I want Project Forza Plus 4, but my launcher is already set up for vanilla

An install cannot be switched to the mod. Set up a second one:

1. Extract `FM4StaticRecomp-Launcher.zip` again, into a **new, empty folder** (for example
   `D:\FM4-PFP4`), not over your current one.
2. Start `FM4StaticRecompLauncher.exe` from the new folder and choose **PROJECT FORZA PLUS 4** at
   its first step. Then follow [Project Forza Plus 4 → Setup](project-forza-plus.md#setup).

Your vanilla install and its saves stay as they are. Start a new career on the mod: vanilla saves
do not work there.

## The game is preparing shaders before the title screen

The first start prepares shaders for your GPU. The window first says **Preparing shaders**,
then shows the number completed and a progress bar. The time depends on your computer and
graphics driver; there is no fixed time limit. You can move, minimize or restore the window
while it works.

Completed work is saved every 10 seconds and when preparation finishes. Later starts reuse it
and are much faster. After an update or a GPU driver update, preparation may be needed again.
If you close the window, it may say **Closing** while the current graphics operation finishes.
Let it close normally. On restart the progress bar starts again, even when saved work makes
preparation faster.

If the window shows **Shader initialization failed**, the game will not start behind the error.
The saved shaders are then usually out of date or damaged. To start again:

1. Close the game window.
2. Open the folder you extracted the launcher into, then `data` → `cache` → the folder named
   after the game version, for example `0.3.3`. On a Mac, start from your data folder instead
   ([Where the game data goes](macos.md#where-the-game-data-goes)).
3. Delete the `shaders` folder there. Leave the other folders alone.
4. Start the game again from the launcher. It now starts without prepared shaders, so the first
   races may stutter briefly while the game compiles them.

If the error comes back, follow [Reporting issues](reporting-issues.md), including the error and
game log.

## The game crashes in a busy moment, a different spot each time

Typical sign: the game closes during or right after a heavy scene (for example the end of the
World Tour Top Gear bowling challenge), and its log has
`Failed to submit a Vulkan command buffer: -4`. The GPU stopped responding.

**Most likely cause: an overclocked or undervolted GPU or CPU.** Short drops and spikes in GPU
load swing the clocks, and a tuned voltage curve that is stable in benchmarks and other games
fails on the swing. On a test PC an overclocked card crashed in 6 of 8 runs of the same
challenge, and in none of 4 at stock settings.

1. In MSI Afterburner, or the tuning app of your graphics card's maker, press **Reset** to go
   back to the default clocks and voltage. Turn off any "OC mode" there as well.
2. If you tuned your CPU in the BIOS (an undervolt or an overclock), set it back to its defaults.
3. Restart the PC and play the same spot again.

If it still crashes at stock settings, [report it](reporting-issues.md#crashes) with REPORT CRASH.

## Stutter, or the game feels uneven at a high frame rate

The game ticks at FRAME RATE whether or not the PC draws every frame, so a rate the PC cannot
hold jumps instead of slowing down evenly. Set **FRAME RATE** on the GRAPHICS page
(**BACK + START** or **F6**) to a rate your PC holds in a full race with AI cars, not on an empty
track. If it still stutters, lower it further or lower **RESOLUTION SCALE**. On a display
without G-Sync or FreeSync, pick the refresh rate or a whole fraction of it (144 Hz: 144 or 72).
More in [High frame rates](high-frame-rate.md#picking-a-rate).

## Sound is slow, late or crackles on an older CPU

Try **SETTINGS → ADVANCED → AUDIO FOR OLDER CPUS** in the launcher, press **SAVE** and restart the game. It is off by default and targets repeated or slowed race audio, most of all on processors without FMA instructions (most made before 2013, and some Celeron and Pentium models). If your sound already works well, leave it off.

The game's sound needs CPU time next to the race; an older or slower CPU that is busy on every
core falls behind. Set **FRAME RATE** to 60, and in Windows **Settings → System → Power &
sleep → Additional power settings** choose **High performance**. Close other programs while
playing. If the sound still crackles, turn on SETTINGS → ADVANCED → **30 FPS (DIAGNOSTIC)** in
the launcher, press SAVE and play a race: say in your report whether the sound is clean at 30 fps.
Then turn it off again and press SAVE.

## Engine sounds pop or click

FM4's engine loops pop where they start over, and click when revving at the limiter and
shifting, on a console too. SETTINGS → ADVANCED → **AUDIO POP FIX** in the launcher smooths them
([#37](https://github.com/i2kiselev/fm4-static-recomp/issues/37)). It is on by default: check that
it is still ticked.

## DLSS or FSR does not turn on (`DLSS (OFF AT 3X)`)

The upscalers only upscale: the render must not be larger than the game's window or screen.
Pick a **RESOLUTION SCALE** up to your screen's size (e.g. 1x or 2x on a 1440p display). A desktop
resolution raised with NVIDIA DSR counts as the screen's size. See
[Graphics settings](graphics.md) and [Upscalers](upscalers.md).

## Tearing

On a display without G-Sync or FreeSync, turn **VSYNC** on in the GRAPHICS page.

## Lossless Scaling or NVIDIA Smooth Motion do nothing

They do not see the game's frames by default. In the launcher, turn on SETTINGS → ADVANCED →
**PRESENT THROUGH DXGI** and press SAVE (see [Frame generation](launch-options.md#frame-generation)).

## Alt-tab is slow and the screen flashes, or Windows HDR is off after playing (NVIDIA)

On some NVIDIA cards the game's fullscreen acts like exclusive fullscreen. Switching to another
window takes a moment and the screen goes black for an instant. Windows HDR can also be off after
the game closes. In the launcher, turn on SETTINGS → ADVANCED → **PRESENT THROUGH DXGI** and press
SAVE.

## A car's picture in the garage is broken (noise, no shadow under the car)

The game draws a car's picture once, when the car arrives in the garage, and keeps it in the
save, so a broken one stays broken. Saves imported from Xenia bring Xenia's pictures with them.
Choose that car, then **PAINT → PAINT CAR**, repaint the body once and save: the game draws the
picture again.

## The controller does nothing at sign-in (NO PROFILE), but the keyboard works

The game gives player 1 to the first controller it finds. Since 0.3.3 it passes over devices that
are not gamepads (drum kits, guitars, dance pads), and while DS4Windows shows your PlayStation
controller as an Xbox controller, the game uses only that one, so presses no longer arrive twice
([#72](https://github.com/i2kiselev/fm4-static-recomp/issues/72)). If the controller still does
nothing:

1. Close the game. Open the Start menu, type **Set up USB game controllers** and open it. The
   list should show one controller for each player.
2. If it shows more, close your controller apps (DS4Windows, Steam, x360ce, the controller maker's
   app): right-click each one's icon beside the clock and choose Exit. Check the list again.
3. Start the game again.

A PlayStation controller works without DS4Windows: the game reads it directly. While DS4Windows or
a similar app (x360ce) is running, the game leaves PlayStation and Switch controllers out and uses
the app's Xbox controller instead. If one of them then does nothing, close that app.

## Linux, Steam Deck or Proton

Not supported yet: the setup fails there (for example "licences could not be read"). A native
Linux build is on the [roadmap](../README.md#roadmap).
