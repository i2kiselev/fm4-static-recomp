# Temporal upscalers: DLSS, FSR 2 and FSR 3

[← Back to the README](../README.md)

The game can render at a lower resolution and have DLSS, FSR 2 or FSR 3 rebuild a sharp picture
at your screen's resolution, or render at the screen's resolution and use them as anti-aliasing
alone. This page says which to pick, and how this port gives a 2011 console game what these
upscalers need.

## Quick start

1. Open the GRAPHICS page: **BACK + START** held together on a controller, **F6**, or Play
   Career → My Profile → Options → GRAPHICS. The launcher's SETTINGS have the same rows.
2. **UPSCALER**: DLSS on an NVIDIA RTX GPU, otherwise FSR 3 or FSR 2 (any GPU). The row skips
   an upscaler your GPU cannot run.
3. **RESOLUTION SCALE**: the render size, from the table below.
4. **SHARPNESS**: 30 % by default; Off to 100 %.
5. **SAVE & APPLY** (**Y**). The upscaler switches at once, MSAA from the next screen.

| Your screen | Scale | Renders | Upscale | Like DLSS's mode |
|---|---|---|--:|---|
| 1920x1080 | 1x | 1280x720 | 1.5x | Quality |
| 2560x1440 | 1x | 1280x720 | 2.0x | Performance |
| 2560x1440 | 2x | 2560x1440 | 1.0x | DLAA (anti-aliasing only) |
| 3840x2160 | 1x | 1280x720 | 3.0x | Ultra Performance |
| 3840x2160 | 2x | 2560x1440 | 1.5x | Quality |
| 3840x2160 | 3x | 3840x2160 | 1.0x | DLAA |

An upscaler only upscales: with one chosen, RESOLUTION SCALE offers the scales whose render
fits the screen. A larger scale saved before, or a window smaller than the render, is
downscaled without the upscaler; the row then reads e.g. `DLSS (OFF AT 3X)`, and MSAA and post
AA apply again.

**DLSS PRESET** (launcher only, while UPSCALER is DLSS): Auto takes preset M where DLSS
upscales by more than 1.8x on an RTX 40 or later GPU, and K otherwise. K, M, J and L force one;
Default leaves the choice to the DLSS runtime and the NVIDIA app.

## FSR 1: the light one

UPSCALER also offers **FSR 1** (AMD FidelityFX Super Resolution 1), on any GPU. It is not
temporal: it upscales the finished picture alone, then sharpens it, at a small cost on the GPU.
MSAA and post AA stay on, and come first: the game draws and resolves its MSAA, post AA (if on)
smooths the picture, then FSR 1 upscales it to the screen. It suits a 1080p screen at 1x
(1280x720 to 1920x1080, e.g. a handheld) where a temporal upscaler costs too much or ghosts.
SHARPNESS sets its sharpening; FSR 1 always sharpens a little, even at Off. Strong sharpening
makes fences and fine nets (e.g. the 599 GTO's grille) shimmer with moiré: the default 30 % or
a little more suits most screens. Post AA softens the picture before FSR 1 sharpens it: try FSR 1
with post AA off first.

## What an upscaler needs

A temporal upscaler builds each frame from many earlier ones. The game draws the scene a
fraction of a pixel off each frame (the *jitter*), so successive frames sample different
points of every pixel, and the upscaler adds them up. To add up the right samples it needs,
for every pixel:

- the **depth**,
- the **motion**: where that point was on screen one frame ago,
- whether the **camera cut** to a new shot, so it drops the old frames.

PC games made for DLSS hand these over. Forza Motorsport 4 was made for the Xbox 360 in 2011
and never computes motion at all. This port reconstructs all three from the GPU commands the
game sends, draw by draw.

## The camera

Each draw call carries its transform matrices in shader constants. The port reads them, tells
the world camera from each object's own matrix, and keeps last frame's copy of both. Motion is
then the difference between where a point lands with this frame's matrices and with last
frame's.

- **Scene depth** comes from the widest surface the game draws into, so a replay's reflection
  capture, drawn more often but smaller, does not take its place.
- **Camera cuts**: the screen's corners are projected with last frame's camera and this
  frame's. When they move by more than a camera can in one frame, it is a cut, and the
  upscaler starts its history afresh. Ordinary fast camera motion keeps the history.
- **The showroom** passes its shaders no separate camera. There, the camera's motion is the
  motion most of the scene's objects share.
- **Races draw the scene turned 180 degrees** in clip space, the showroom upright; the jitter's
  sign follows, or the samples would not line up.

## Jitter

Only the 3D scene is jittered: draws that test against the scene's depth or write its colour.
Everything the game draws after its final post-processing pass is the HUD or a menu: it is
neither jittered nor upscaled as scene. The port copies the frame just before the HUD for the
upscaler and lays the HUD over the upscaled picture, so speed and position text stays still
instead of boiling.

## Things that move on their own

The camera's motion alone would smear everything that moves differently from it. Each moving
object gets its own motion, from its own matrix this frame and last; the same object is found
again in the next frame by its mesh and its place among the draws.

- **Cars**: every body, its parts and its lamps. Car parts that read the camera's constants as
  plain numbers (a passing car's exhaust tips) are placed by their own matrix.
- **The driver**, animated by a bone palette, takes the car's motion.
- **Wheels** move with their hub, but without their spin: a wheel turns too far between two
  frames for any upscaler to follow.
- **Ground shadows**: the player car's shadow moves with its body, other cars' shadows with
  their cars.
- **Cockpit view**: the cabin moves with the camera, so it takes its own motion, not the
  track's; the dash's displays are textures the game draws, not scene.

## What changes while an upscaler runs

| | Why |
|---|---|
| **MSAA off** (MSAA rows show `OFF (DLSS)`) | The upscaler smooths edges itself. The rows keep your choice for when the upscaler is off. |
| **FXAA off** | It would blur the upscaler's input. |
| **Replay camera motion blur off** | The upscaler would turn its smear around mirrors and exhaust pipes into ghosting. `--fm4_upscaler_replay_blur=true` keeps it. |
| **Texture detail of the output size** | Textures pick their mip level as if rendered at the output resolution (a LOD bias of `log2(render / output)`, -1 from 720p to 1440p), so the upscaled picture is not blurry. |
| **Sharper car shadows at 1x** | Taking a 1x frame, the races' ground shadow map is drawn at 512 instead of 256 and the car's own shadow map at 768 instead of 512, so their edges are not blocky after a 2x upscale. |
| **One sharpening pass** | SHARPNESS drives one RCAS pass after the upscaler, for all three; FSR's built-in sharpening is off, so nothing is sharpened twice. |
| **Mirrors from the frame** | The rear-view mirrors (the cockpit's, the hood camera's at the top of the screen) show what the mirror's own camera sees, which moves differently from the glass, so no motion lets the upscaler follow it and it smeared. The glass is drawn without jitter and put back over the upscale from the render, as the HUD is; its rim stays the upscaler's, for smooth edges. `--present_upscaler_mirrors=false` turns this off. |
| **Steady cut-outs** | Foliage, fences and crowds are alpha-to-coverage cut-outs; at 1x they take one steady threshold, where a dither would flicker under the jitter. |

## Shaders

The extra pipelines an upscaler needs are part of the shader cache setup builds from your disc
(4,819 pipelines, recorded from a DLSS playthrough). A combination it lacks builds in the
background while the game draws with the ordinary pipeline, instead of freezing.

## Known limitations

- **The side mirrors seen from the chase camera** look too reflective.
- **The mirrors** are at the render resolution, scaled up, like the HUD.
- **Thin details** shimmer in places: road cracks and painted lines in the distance, distant
  fences, trees and crowds.
- **The HUD and menus** stay at the render resolution, scaled up; the 3D picture behind a
  semi-transparent HUD panel is the render-resolution frame.

Seen something else? [Report it](reporting-issues.md); a `-Compat` comparison or one of the
debug views below helps.

## Debug views

`--present_upscaler_view=motion` shows the motion the upscaler gets (grey is still, red and
green are motion), `depth` its depth; `--present_upscaler_object_debug=1` colours each moving
object's slot. Every fix above has an on/off switch for comparison: see
[DEBUG_OPTIONS.md](../DEBUG_OPTIONS.md#temporal-upscalers-dlss-fsr-23) and
[setting options](launch-options.md#setting-options-and-debugging).
