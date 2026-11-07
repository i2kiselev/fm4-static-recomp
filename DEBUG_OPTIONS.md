# Debug and A/B options

Options for checking a rendering problem, comparing a fix against the picture without it, or
measuring a cost. They are not needed to play; the defaults are the intended setting. Set them
like the [launch options](guide/launch-options.md): with the launcher through `REX_`
environment variables, else in `fm4_static_recomp.toml` or on the command line, for example
`tools\run.ps1 -GameArguments '--fm4_skip_intro=true','--present_upscaler_view=motion'`
(examples of each in [Setting options and debugging](guide/launch-options.md#setting-options-and-debugging)).

Most come from the ShiftGlue SDK's Vulkan renderer. The ones marked "wrong image" break the
picture on purpose, to bound what one part of the frame costs.

## Reading the FPS overlay

`--fm4_show_fps=true` opens the debug overlay. **Guest** counts new guest-output
notifications over at least 250 ms of monotonic host time, independently of UI redraws.
It measures guest-image throughput, not the configured frame-rate cap or confirmed display
scanout. On Vulkan the notification follows submission/publication of the guest image;
the host still has to present it. The displayed milliseconds are `1000 / FPS`, an average
interval rather than a measurement of individual frame times.

At 60 FPS, a quarter-second window contains about 15 images. One image falling on the
other side of a sampling boundary changes the reading by about 4 FPS. Small fluctuations
can therefore occur with periodic output; a steady average can also hide a stall followed
by a burst. Neither smooth-looking gameplay nor this average alone establishes stable
60 FPS pacing.

For timing investigations, compare the same scene, cap, output size, upscaler and warmed
cache with Vulkan validation enabled and disabled. The SDK option
`--perf_critical_path_trace=true` logs monotonic timestamps for `guest_swap`,
`consumed_swap` and host `present` events. Compare interval distributions and published
image sequences as well as average rates. Host `present` records a successful presentation
API return, not a hardware scanout timestamp. Tracing and validation add work, so repeat
measurements and include a tracing-disabled control. Keep diagnostic logs private.

## Temporal upscalers (DLSS, FSR 2/3)

Switches of the fixes that give the upscaler correct motion. Turn one off to see what it fixes.

| Option | Default | What it does |
|---|---|---|
| `--present_upscaler_camera` | `true` | Give the upscaler the game's depth and camera motion, taken from its draws (`false`: far depth, no motion) |
| `--present_upscaler_jitter` | `true` | Jitter the scene by a sub-pixel offset each frame |
| `--present_upscaler_object_motion` | `true` | Give each moving object (cars, wheels, the driver) its own motion, not only the camera's |
| `--present_upscaler_spin_free_wheels` | `true` | Spinning wheels move with their hub, without the spin, which is too fast to follow |
| `--present_upscaler_shadow_motion` | `true` | Live. Where the player car's ground shadow's edges swept over the ground, a temporal upscaler gets the history of the road the other way (none where that road is lit differently or an object lies on it); the asphalt elsewhere keeps the ground's motion. Off under DLSS preset K, which trails along that band (light trails with motion blur off, a dark outline with it on) |
| `--present_upscaler_uncovered_ground` | `true` | Live. Ground that was under a car a frame ago gets the history of the road the other way from a temporal upscaler (none where that road is lit differently or an object lies on it; `false`: at speed DLSS preset M smudges the road behind the car with a copy of its lower body and dark blocks from its tyres). Off under DLSS preset K, which trails around the car's lower body and mirrors with it and shows no tyre blocks without it |
| `--present_upscaler_decal_motion` | `true` | Other cars' ground shadows move with their cars |
| `--present_upscaler_mirrors` | `true` | Live. The rear-view mirrors (cockpit, and the hood camera's mirror at the top) are drawn unjittered and put back after the upscale from the frame, as the HUD: their reflections move with the mirror's own camera, which the upscaler cannot follow, and smeared |
| `--present_upscaler_uniform_alpha_to_coverage` | `false` | Above 1x, cut-outs (foliage, crowds, fences) take one steady threshold instead of an ordered dither, which can show as hatching on leaf edges |
| `--fm4_upscaler_replay_blur` | `false` | Keep the replay cameras' motion blur while an upscaler runs (`false`: no blur, whose smear around the car's mirrors and exhaust pipes the upscaler turns into ghosting) |
| `--present_upscaler_hud` | `composite` | What the upscaler gets where the HUD is: `composite` (the frame without the HUD, the HUD drawn after the upscale), `off` (the frame with the HUD) or `debug` (the frame without the HUD, shown so) |
| `--present_upscaler_cut_out_no_bias` | `true` | Live. Scene draws with alpha to coverage (foliage, crowds, fences) sample without the upscaler's texture LOD bias: with it, the cut-outs change shape in motion and leaf clusters pop. `false`: they take the bias as other scene draws do |
| `--present_upscaler_mip_bias` | `0` | Live. Added to the texture LOD bias an upscaler gives, `log2(render / output width)` (-1 from 720p to 1440p), with which textures keep the output's detail: negative is sharper and can shimmer (the DLSS and FSR guides recommend `-1`), positive is softer. Alpha to coverage draws leave all of it out while `--present_upscaler_cut_out_no_bias` is on |
| `--fm4_shadow_map_size` | `0` | Car shadow map size from the next scene load: `0` is 768 while an upscaler takes a 1x frame, else 512 (the console's); `512` or `768` fixes it |
| `--fm4_ground_shadow_size` | `0` | Races' ground shadow map size (the car seen from the sun) from the next scene load: `0` is 512 while an upscaler takes a 1x frame, else 256 (the console's); `256` to `768` fixes it |
| `--present_dlss_runtime` | empty | Folder holding `nvngx_dlss.dll`; empty uses the newest one next to the executable |
| `--present_fsr_quality_mode` | `auto` | FSR mode: `auto`, `nativeaa`, `quality`, `balanced`, `performance`, `ultra_performance` |
| `--present_fsr_sharpness_reduction` | set by SHARPNESS | FSR 1's RCAS sharpening reduction, in stops: SHARPNESS sets it to `2 × (1 − sharpness)` (`1.4` at 30 %); given on the command line, it stays as given |
| `--present_upscaler_supersample_temp` | `false` | With the render larger than the output (supersampling), the temporal upscaler runs 1:1 at the render size and a bilinear pass brings it down to the output (`false`: the upscaler scales straight to the output) |

Views and measurements:

| Option | Default | What it does |
|---|---|---|
| `--present_upscaler_view` | `upscale` | Show one of the upscaler's inputs instead of its output: `depth` (near white, far black) or `motion` (red and green around grey) |
| `--present_upscaler_object_debug` | `0` | In the motion view: `1` object slots, `2` the reactive mask, `3` other cars' ground-shadow slots (`2` needs `--present_upscaler_reactive_mask` or `--present_upscaler_reactive_shadows`, `3` `--present_upscaler_decal_motion`) |
| `--present_upscaler_reactive_mask` | `false` | Mark alpha-blended scene draws (car glass, tail lights, tyre marks) in the reactive mask |
| `--present_upscaler_reactive_shadows` | `false` | Mark the player car's ground shadow in the reactive mask |
| `--present_upscaler_profile` | `false` | Time the upscaler on the GPU and log the mean of every 300 upscales |
| `--present_dlss_jitter_sign` | `0` | Negate the jitter DLSS is given: `1` x, `2` y, `3` both |

## The game

| Option | Default | What it does |
|---|---|---|
| `--fm4_console_rate` | `false` | The console's pacing: races at 60 fps, the showroom and other half-rate scenes at 30 |
| `--fm4_frame_rate=30..59` | `120` | A frame rate below the lowest a player picks (60), for testing slow processors; the launcher's SETTINGS → ADVANCED → **30 FPS (DIAGNOSTIC)** passes `30`. Each car still steps about every 1/60 s: the game runs it twice a tick |
| `--fm4_menu_vblank_hz` | `0` | Vblanks per second the game's main loop sees outside races (`0`: every vblank) |
| `--fm4_physics_log` | empty | A CSV file receiving every main loop time step and each car's physics substeps |
| `--fm4_car_min_substeps` | `0` | A car's physics substeps per tick are at least this many; the launcher passes `6` on Project Forza Plus 4 |
| `--fm4_hook_census` | `false` | Log each of the game code hooks once, on its first hit (`Hook census: <name>`): the render tests check that a Rev 1 build reaches the hooks a 1.0 build does |
| `--fm4_render_scenario_omit` | empty | Render settings left to the game, comma-separated: `tiling`, `lod_thresholds`, `focus_lod`, `other_lod`, `lod_distances`, ... |
| `--fm4_graphics_lod_thresholds` | empty | Car level-of-detail rank thresholds of the high and ultra settings, as `lod1,lod2,count` |
| `--fm4_runtime_replacement_probe` | empty | Replace the texture with this 16-digit hash by a magenta test image |
| `--debug_guest_thread_start_delay` | empty | `<hex start address>:<ms>`: every guest thread starting there waits that long before it runs. `82926250:500` holds back the studio tracks' streaming thread, as the host now and then does on its own; a bought car's thumbnail must still come out lit (the `thumbnail-late-streaming` render test) |
| `--xnet_random_constant` | `false` | The game's random bytes (`XNetRandom`) all `0xBB` again, as before 0.3.2: the race music then starts with the same song every time (#57), and garage car GUIDs, AI player IDs and save encryption IVs are constant too |
| `--xma_loop_end_crossfade` | set by AUDIO POP FIX | Crossfade the end of each pass of an XMA stream into the 384 samples FMOD skips when it restarts the stream (#37); AUDIO POP FIX sets it, and given on the command line it stays as given |
| `--xma_stream_start_resets_carry` | `true` | A stream's first frame drops the samples the decoder kept from the stream before, as the hardware does; `false` joins them to the new stream (#37) |
| `--xma_frameless_end_of_stream` | `true` | The packets after a stream's last frame that start no frame end the input, as on the hardware; `false` flags error 4 and decodes the buffer again from its start (#37) |
| `--xma_decode_kicks_inline` | `true` | Decode game audio on the game thread that starts it; `false` decodes it on the XMA decoder thread instead; the mixer then now and then reads a block before it is decoded, heard as crackle (#37) |

## The renderer

Draws, by their index in the frame or their shader hash:

| Option | What it does |
|---|---|
| `--fh1_debug_log_draws` | Log every draw's index, primitive, index count and shader hashes |
| `--fh1_draw_log_frame=<n>` | Log every draw of frame `n` in detail (targets, blend, textures) and its resolves |
| `--fh1_debug_log_ps=<hash>`, `--fh1_debug_log_vs=<hash>` | Log the constants of every draw with that pixel or vertex shader |
| `--fh1_debug_skip_draws=<first>-<last>,...` | Skip those draws in every frame; can change while the game runs (`SET:`), to bisect a paused frame |
| `--fh1_debug_null_fetch=<draw>:<fetch>` | That draw sees that texture fetch constant as a null texture |
| `--fh1_debug_vs_constant=<hash>:c<n>=x,y,z,w` | Replace one float constant of that vertex shader |
| `--fh1_debug_dump_range=<draw>:<address>:<length>` | Before that draw, write those guest bytes as the GPU has them to the resolve dump folder |

Costs (wrong image unless noted):

| Option | What it does |
|---|---|
| `--fh1_debug_tiny_draws` | Draw only the first triangle of every draw |
| `--fh1_debug_skip_draw_calls` | Record every draw's state but not its draw command |
| `--fh1_debug_keep_constant_binds` | Do not rebind constants when only their offsets change |
| `--fh1_debug_rendering_split_draws=<n>` | End the rendering every `n` draws (same image) |
| `--fh1_debug_skip_clears` | Skip every EDRAM clear |
| `--fh1_debug_skip_transfers=<source>=<destination>` | Skip transfers between those surfaces |
| `--fh1_debug_skip_stencil_transfers` | Transfer depth without its stencil |
| `--fh1_debug_skip_resolve_dispatches` | Keep each resolve's barriers but not its compute dispatch |
| `--fh1_debug_skip_resolve_reloads`, `--fh1_debug_skip_resolve_reloads_format=<n>` | Never reload textures from resolved memory (stale images) |
| `--fh1_native_gpu_profile_draws` | With `--fh1_native_gpu_profile`, also time every draw by its pixel shader |
| `--fh1_frame_replay_repeat=<n>` | Run a replayed frame `n` times, as a GPU benchmark |
| `--fh1_untile_predicated_tiling=true` | Live. Combine predicated tile passes into one draw pass. FM4 disables this on macOS, matching Windows: enabling it can clip the default 4x MSAA menus and leaves stale image bands |
| `--fh1_debug_untile=1\|2` | Diagnostics of the single-pass tiling: keep the first tile's predication, or draw the later tiles too |

Resolves and visibility:

| Option | What it does |
|---|---|
| `--fh1_resolve_dump_dir=<folder>`, `--fh1_resolve_dump_frame=<n>` | Write every resolve's output bytes to that folder (only frame `n` if given) |
| `--fh1_hold_resolve_sync` | While a car thumbnail is made (`--fh1_readback_all_resolves_hold` set): a full memory barrier and a wait for the GPU after every resolve, its readbacks copied into guest memory at once (Vulkan). On by default in FM4, frame drops while a thumbnail is made; `false` turns it off |
| `--fh1_resolve_dump_hold` | With `--fh1_resolve_dump_dir`: dump only while a car thumbnail is made (`--fh1_readback_all_resolves_hold` set), at a resolution scale also the scaled bytes, and log each dump's format, pitch and rectangle (Vulkan) |
| `--fh1_readback_all_resolves` | Copy every resolve into guest memory, not only the ones the CPU reads (slow) |
| `--fh1_readback_all_resolves_hold` | Live. The same while set; the game sets it from a car thumbnail's render until it is saved, and clears it after, so a launch value does not last |
| `--fh1_log_resolve_readbacks` | Log runs of resolves into a range and whether they are read back |
| `--fh1_async_recurring_readbacks=false` | Live. Let a capture the game keeps re-rendering (the cars' environment captures) wait for the whole GPU queue again when it comes back after an idle gap, as before; the default (`true`) copies it back without that wait, which was a 5-15 ms hitch mid-race |
| `--fh1_specialize_edram_passes` | Resolves and EDRAM transfers use pipelines built for each kind (formats, MSAA, pitches) instead of one generic pipeline. On by default on Intel GPUs, where the generic one makes lights flicker; off on others |
| `--fh1_trace_resolve_cpu_accesses` | Log guest CPU reads and writes of each resolved range |
| `--fh1_trace_direct_resolves` | Count why textures over a resolve's destination are not written by it directly |
| `--fh1_log_scaled_textures` | Log every texture created from scaled resolve data |
| `--fh1_visibility_census=<n>` | Every `n` frames, an occlusion query around each draw, logging how many draw nothing |
| `--fh1_visibility_census_no_depth` | Census frames draw without the depth test |
| `--fh1_visibility_oracle_skip`, `--fh1_visibility_oracle_late_only` | Drop draws that drew nothing at their last census (wrong image) |
| `--vulkan_verify_draw_fetches` | Live. While a car thumbnail renders (until it is saved), copy back the GPU's copy of every vertex buffer and index range each draw reads, at the draw, and log the ranges that differ from the game's memory at record time, with their page state; index buffers converted on the CPU (16-bit strips with a reset index, the cars') are compared with the game's indices too; a summary per thumbnail. Slow during the thumbnail only |
| `--vulkan_verify_draw_fetches_list` | With the option above, also write every draw of each thumbnail window (shaders, index type and count, each vertex range with its page state) to `fetch-window-<n>.txt` beside the log file, to compare two thumbnails' renders draw by draw |

GPU crashes (device lost, `Failed to submit a Vulkan command buffer: -4`):

| Option | What it does |
|---|---|
| `--vulkan_device_address_bindings` | Track which objects the driver places at which GPU addresses; after a device loss the log names what was at, or was last freed from, each faulting address, and the driver's crash data, when the driver provides it, is saved as `vulkan-device-fault.bin` beside the log (the fault's description and addresses are logged without the option) |
| `--vulkan_diagnostic_checkpoints` | NVIDIA only. Mark every draw, resolve, clear, copy, texture load, compute pass (resolves into guest memory with their destination) and the presenter's paint in the command stream; after a device loss the log lists the last marks the GPU began and finished, and the work in between |
| `--vulkan_diagnostic_checkpoints_serialize` | With the option above, the GPU finishes all earlier work before each mark outside a render pass, and every submission starts with a mark: the report then names the work that faulted, not only how far the command stream got while earlier work still ran. Slow |
