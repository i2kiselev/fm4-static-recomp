# Neural Rendering

[← Back to the README](../README.md)

Neural Rendering is an optional NVIDIA effect that runs a neural model over the 3D scene before
DLSS, FSR 2, or FSR 3 upscales it. It is off by default. It needs an NVIDIA RTX GPU, the SDK's
DLSS provider, and a compatible Neural Rendering runtime supplied by the user.

## Requirements and setup

1. Use an FM4 build made with a ShiftGlue SDK that includes the Neural Rendering bridge. Its
   source is MIT-licensed; the built bridge also contains NVIDIA's separately licensed parameter
   allocator, and the SDK package includes both licence texts.
2. Obtain a compatible `nvngx_dlssnr.dll`. It is not included in FM4 or the SDK package.
3. Put the runtime at `upscalers\dlss-nr\nvngx_dlssnr.dll` beside the game executable.
4. Choose DLSS, FSR 2, or FSR 3 as the UPSCALER. NR does not run without a temporal upscaler.
5. Set **NEURAL RENDERING** to **ON** on the GRAPHICS page and restart the game.

`FM4_NR_RUNTIME` may name a different folder containing the runtime for a local session.
The GRAPHICS page reports whether the bridge and runtime are installed, whether the model is
loading, and whether it is active.

## Settings

| Setting | Effect |
|---|---|
| NEURAL RENDERING | Enables the model and applies the change after a restart. |
| NR MODEL | Automatic uses the runtime default. Original selects the original model. Optimized selects the FP8 model when the installed runtime supports it. |
| NR STYLE | One of the runtime's three visual styles. |
| NR INTENSITY | Strength of the effect, from 0 to 200%. |
| NR STRUCTURE | Strength of local structure adjustment, from 0 to 200%. |
| NR TONE | Strength of local tone adjustment, from 0 to 200%. |

Model and tuning changes rebuild the model history. The game keeps drawing with the ordinary
upscaled frame while the model loads or if any step fails.

## How FM4 feeds it

FM4's guest frame is converted from the renderer's packed 10-bit RGB format to FP16 at render
resolution. The existing game-derived depth and motion data are used directly; motion is changed
from UV units to render pixels. The NR result goes back to the selected temporal upscaler with
the original alpha channel preserved, so the upscaler's HUD composition continues to work.

If the bridge is absent, the runtime is missing or incompatible, the GPU is not supported, or a
frame has an unexpected format, the original frame and temporal upscaler continue normally.

## Privacy and files

The SDK does not download `nvngx_dlssnr.dll`. NVIDIA's model and runtime remain under NVIDIA's
terms and are not redistributed by this project. The bridge's MIT-licensed source, ABI, and
provenance are in the ShiftGlue SDK repository; its binary also contains NVIDIA's parameter
allocator under the NVIDIA RTX SDK terms included with the SDK package.
