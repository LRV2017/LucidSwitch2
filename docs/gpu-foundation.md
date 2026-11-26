# GPU and graphics foundation

Phase 4 introduces a small clean-room GPU command path and a renderer boundary.

## Guest GPU memory

GuestGpuMemory provides a bounded guest-visible address range with checked read, write, and fill operations. It currently uses host-backed storage. It does not model a specific console's physical memory controller, GPU page tables, compression, tiling, or cache coherence.

## Command buffer format

A command header is one little-endian uint32:
- bits 31..24: opcode
- bits 23..0: number of following uint32 payload words

Opcodes:
| Opcode | Value | Payload |
|---|---:|---|
| NOP | 0x00 | none |
| BEGIN_FRAME | 0x01 | width, height |
| CLEAR | 0x02 | packed RGBA color |
| DRAW_RECT | 0x03 | x, y, width, height, packed RGBA color |
| PRESENT | 0x04 | none |
| END | 0xFF | none |

Colors are encoded as 0xRRGGBBAA. Dimensions are limited to 8192 by the command processor; the software reference backend also caps allocations at 16 megapixels. Rectangles must have a nonzero size and remain within the active frame.

The processor validates the full command stream, including payload sizes, ordering, dimensions, rectangle bounds, and the final END marker, before dispatching commands to a backend. Command buffers are capped at 1 MiB.

## Renderer backends

- SoftwareBackend is a deterministic CPU reference rasterizer that supports frame begin, color clear, filled rectangles, present, and pixel readback.
- VulkanBackend is optional at CMake configure time. If the Vulkan SDK is detected, it creates a Vulkan instance, selects a graphics-capable physical device, creates a logical device and command queue, allocates a device-local RGBA8 off-screen image, and uploads completed reference frames using a staging buffer and transfer command. It reports the selected Vulkan device in its name.
- The current Vulkan path is intentionally an upload/device-integration backend: command rasterization is still performed by the software reference renderer. It does not yet include GPU shader pipelines, draw calls, a window surface, a swapchain, or hardware rasterization. On a machine without Vulkan development files, the same API builds as an explicit unavailable stub.

## What this does not implement

There is no Switch 2 GPU command compatibility, GPU firmware, proprietary macrocode, shader translation, guest texture formats, tiled memory, render-target compression, synchronization primitives, compute, or game rendering compatibility. No console game data or proprietary assets are bundled.

## Build

The normal build automatically detects a Vulkan SDK and enables the optional backend. On Windows, install the Vulkan SDK from LunarG and ensure the graphics driver exposes Vulkan, then reconfigure the project. The command-line build and software renderer do not require Vulkan.

To check whether Vulkan development support was detected, inspect the CMake configure output and the GPU foundation test result.


The executable includes a `--gpu-info` diagnostic. It reports whether Vulkan development support was detected during CMake configuration and, when available, attempts to initialize the Vulkan device and reports its name.