# Frame generation and Reflex

FM4 supports one generated frame per rendered frame (2x) with NVIDIA DLSS Frame Generation or
AMD FSR 3.1.6 Frame Generation. NVIDIA Reflex can run with or without generation. These features
require the Vulkan renderer; the D3D12 renderer does not provide them.

## Requirements

- A Vulkan 1.2-capable GPU exposed through an up-to-date driver.
- For DLSS Frame Generation: a supported NVIDIA RTX Ada GPU and the bundled DLSS 310.9.16
  runtime.
- For FSR Frame Generation: a Vulkan device supporting compute shader derivatives and the
  FidelityFX renderer requirements.
- For Reflex: NVIDIA's `VK_NV_low_latency2`, present-wait and timeline-semaphore support.

The game checks these capabilities at runtime. An unsupported selection keeps the original frame
path active and writes the reason to the log. A selectable menu item is not proof that a provider
became active.

## Settings

The in-game **GRAPHICS** page exposes:

| Setting | Values | Default |
| --- | --- | --- |
| Frame generation | Off, DLSS, FSR | Off |
| FG multiplier | 2x | 2x |
| NVIDIA Reflex | Off, On, On + Boost | On |

Changes apply after restarting the game. The same values can be passed on the command line:

```text
--present_frame_generation=off|dlss|fsr
--present_frame_generation_multiplier=2
--present_reflex=off|on|boost
```

## Behaviour

The renderer uses the HUD-free, upscaled scene and the current depth and camera motion. The
generated midpoint is presented before the current real frame. It is paced against the measured
guest-frame interval, and a late midpoint is skipped rather than presented in a burst.

Reflex samples input from the guest's real controller read. Driver pacing completes before that
sample, and the input, simulation, render and presentation markers share one application-frame
identity across the generated/real pair.

Generation requires camera matrices that can be validated as projection data. The game keeps
ordinary presentation active when those guides are unavailable, such as in menus or on unsupported
camera layouts.

## Diagnostics

Set `present_fg_timing=true` or `BB_FG_TIMING=1` to log source and physical-present cadence,
generated and skipped counts, and minimum, maximum and mean presentation gaps every 120 source
frames. The status text on the graphics page reports the live provider state.

See [upscalers.md](upscalers.md) for reconstruction quality controls and
[graphics.md](graphics.md) for the other Vulkan settings.
