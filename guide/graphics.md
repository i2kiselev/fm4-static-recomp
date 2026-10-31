# Graphics settings

[← Back to the README](../README.md)

**Play Career → My Profile → Options → GRAPHICS**, or **F6** or **BACK + START** held
together on a controller anywhere, opens a settings page
in the game's own style. Each row explains itself; a changed row is marked red until **SAVE &
APPLY** (**Y** on a controller, the menu's Y key on the keyboard, **Y** by default) saves the
changes to `config\graphics.toml` in the state folder and applies them one after another. The
page then says the settings are applied; a row's change takes effect at once or from the next
race or screen, as the row says. Leaving with unapplied changes asks once more; leaving again
discards them. A
[launch option](launch-options.md) for one of these settings sets its starting value for that
session only.

## Presets

GRAPHICS PRESET sets every row below it; changing one of them makes the preset CUSTOM.
FRAME RATE, RESOLUTION SCALE, UPSCALER and SHARPNESS are not part of a preset.

| Setting | Console | High | Ultra | Experimental |
|---|:---:|:---:|:---:|:---:|
| Showroom MSAA | 4x | 2x | 2x | 4x |
| Race MSAA | 2x | 2x | 2x | 4x |
| Anisotropic filtering | Game | 16x | 16x | 16x |
| Level of detail | Console | High | Ultra | Ultra |
| Scenery shadows | Off | Off | Off | On |
| Reflection updates | Console | Console | Every race | Double |
| Mirror | Console | Console | Full | Full |
| Soft particles | Off | Off | On | On |
| Crowds | Console | Console | Console | Every race |
| Post AA | Off | FXAA | FXAA Extreme | FXAA Extreme |
| Bloom | Console | Full rate | Full rate | Full rate |
| Motion blur | On | On | On | On |
| Depth of field | On | On | On | On |
| Car dirt | On | On | On | On |

## What each setting does

### Display

| Setting | Effect |
|---|---|
| FRAME RATE | Frame rate of races, menus and the showroom, 60 fps up to your display's refresh rate (at most 240). Pick a rate your PC holds (see [Benchmarks](performance.md#benchmarks)). Without G-Sync or FreeSync, pick the refresh rate or a whole fraction of it. How it works: [High frame rates](high-frame-rate.md). |
| VSYNC | Off by default, which suits G-Sync and FreeSync displays. Turn it on if you see tearing. |
| RESOLUTION SCALE | 1x to 4x of 1280x720 on each axis, up to 5120x2880. |

### Anti-aliasing and textures

| Setting | Effect |
|---|---|
| UPSCALER | Off stretches the picture to your display. DLSS (NVIDIA RTX GPUs), FSR 2 or FSR 3 (any GPU) upscale it from the resolution scale with the game's depth and motion and smooth edges themselves, so MSAA and post AA are off while one runs. With the output at the render size they work as anti-aliasing alone (DLAA). They only upscale: while one is chosen, RESOLUTION SCALE offers the scales up to the screen's size (1x and 2x on a 1440p display), and choosing one over a larger scale lowers the scale with it, shown as a change before SAVE & APPLY. The launcher's SETTINGS follow the same rule for the display the launcher is on. A larger scale saved before, or a game window or screen smaller than that display, is downscaled without the upscaler: the row then reads e.g. `DLSS (OFF AT 3X)` and MSAA and post AA apply again. Applies at once; the MSAA change from the next screen. How they work and which to pick: [Upscalers](upscalers.md). |
| SHARPNESS | Sharpening after DLSS, FSR 2 or FSR 3: Off, or 10 % to 100 %; 30 % by default. Applies at once. |
| DLSS PRESET | In the launcher only, while UPSCALER is DLSS: the DLSS model. Auto takes M where DLSS upscales by more than 1.8x on an RTX 40 or later GPU, else K; K, M, J and L pick one; Default leaves the choice to the DLSS runtime and the NVIDIA app's override. From the next start. |
| FRAME GENERATION | Off, DLSS or FSR. Produces one interpolated frame per rendered frame (2x) using the current depth, motion and camera data. Requires a restart; an unsupported runtime leaves ordinary frames active. Details: [Frame generation and Reflex](frame-generation.md). |
| FG MULTIPLIER | 2x. One generated frame is presented between source frames. Requires a restart. |
| NVIDIA REFLEX | Off, On or On + Boost on supported NVIDIA GPUs. On paces the next input sample after the previous source work completes; Boost also requests the driver's high-performance mode. Requires a restart. |
| SHOWROOM MSAA | The game's own multisampling in the showroom, menus, pre-race, replays and photo mode: 4x as on the console, 2x or off. Fewer samples are faster and leave edge smoothing to the resolution scale. From the next screen. |
| RACE MSAA | The same in races, the countdown and the results: 2x as on the console (4x in races alone on track), 4x (heavy on the GPU) or off. From the next race. |
| POST AA | FXAA or FXAA Extreme over the finished picture; Extreme smooths and softens more. Off while an upscaler runs. |
| ANISOTROPIC FILTERING | Keeps textures sharp at grazing angles. Game keeps what the game asks for; 2x to 16x force it. |

### Scene detail

| Setting | Effect |
|---|---|
| LEVEL OF DETAIL | How far away, and for how many cars, detailed models stay in use. High moves the switch distances out about 1.4 times, Ultra about twice and keeps your own car on its best model. From the next race load. |
| REFLECTION UPDATES | How often car paint, glass and chrome reflections are redrawn. The console freezes them in 16-car races and split screen; Every race redraws them there too; Double redraws twice as much each frame. |
| MIRROR | Full draws the rear-view mirror every frame with up to 16 cars, where the console halves its rate and shows 6 cars in large races. |
| SCENERY SHADOWS | Experimental, only in the Experimental preset. Shadows of cones, barriers and other scenery in races, otherwise only in replays and photo mode. |
| CROWDS | Every race adds the crowds the console leaves out of 1- and 16-car races. |
| CAR DIRT | The dirt cars gather in a race. Off leaves cars clean. |

> [!WARNING]
> SCENERY SHADOWS has a known bug: a shadow vanishes or flickers as its object nears the edge
> of the screen, most visibly on large objects
> ([#3](https://github.com/i2kiselev/fm4-static-recomp/issues/3)).

### Effects

| Setting | Effect |
|---|---|
| SOFT PARTICLES | Smoke and dust fade into the ground and cars instead of cutting into them. |
| BLOOM | Full rate updates the glow every frame, where the console skips every second one in 16-car races. |
| MOTION BLUR | The speed blur on the road surface. |
| DEPTH OF FIELD | The focus blur of the showroom, pre-race and replay cameras and photo mode. |
