# Graphics pipeline foundation

Phases 5 and 7 build an original host graphics API and two interchangeable implementations: a deterministic CPU reference renderer and an optional native Vulkan renderer. Neither implementation emulates a console GPU command processor or proprietary graphics API.

## Shared graphics contract

- GraphicsVertex contains pixel-space X/Y, normalized depth Z, packed 0xRRGGBBAA color, and U/V texture coordinates.
- Indexed geometry uses uint32 triangle-list indices.
- A render target contains an RGBA8 color attachment and a floating-point depth attachment.
- Textures use stable nonzero IDs. Samplers support nearest or linear filtering and clamp or repeat addressing.
- The original guest command stream validates frame, texture, vertex, index, and depth operations before dispatch.
- SoftwareBackend is the deterministic CPU reference renderer and remains the fallback when native Vulkan tools or devices are unavailable.

## Native Vulkan implementation (Phase 7)

When CMake detects the Vulkan SDK and glslc, it compiles the project-owned GLSL shaders to SPIR-V and enables VulkanBackend. The implementation now performs the graphics work through Vulkan rather than delegating rasterization to the software backend.

The native path includes:

- Vulkan instance and physical/logical device selection, preferring discrete GPUs and then integrated GPUs.
- A graphics queue and command pool with explicit command-buffer recording and submission.
- A Vulkan graphics render pass and graphics pipeline with vertex and fragment shader stages, dynamic viewport/scissor, triangle-list input assembly, and depth testing/writes using less-or-equal comparison.
- Vulkan vertex and index buffers consumed by the GPU input assembler.
- Device-local RGBA8 sampled texture images, image-layout transitions, Vulkan samplers, descriptor sets, and the fragment-shader texture binding.
- Device-local RGBA8 color and D32 (or supported D24/S8) depth attachments.
- GPU framebuffer rendering followed by image-to-buffer transfer and CPU readback for verification.
- Optional SDL2 Vulkan window integration: SDL supplies the platform surface extensions and creates a VkSurfaceKHR; the backend creates a Vulkan swapchain, acquires images, blits the completed GPU render target into the acquired image, and presents it using semaphores.
- Swapchain recreation after SDL resize notifications or an out-of-date acquisition/presentation result.

The graphics shaders are original project code in shaders/lucid.vert and shaders/lucid.frag. CMake builds lucid.vert.spv and lucid.frag.spv into the build tree. The runtime loads those compiled SPIR-V files from the configured build shader directory.

The SDL2 swapchain path presents a GPU-rendered image. It is separate from the earlier SDL renderer preview that uploads CPU readback pixels. The windowed backend currently renders to an off-screen color/depth target, then uses a Vulkan image blit to the acquired swapchain image.

## Configure and run

A native pipeline build requires:
- Vulkan SDK / Vulkan loader and development headers
- glslc from the Vulkan SDK or shaderc toolchain
- C++20 compiler and CMake 3.20+
- Optional SDL2 development files for Vulkan swapchain presentation

CMake automatically keeps the software renderer enabled if the Vulkan SDK or glslc is not available.

Windows (PowerShell):

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release --parallel
    .\build\Release\lucidswitch2.exe --gpu-info
    .\build\Release\lucidswitch2.exe --graphics-demo --vulkan
    .\build\Release\lucidswitch2.exe --graphics-demo --window --vulkan

Linux:

    sudo apt install libvulkan-dev mesa-vulkan-drivers glslc libsdl2-dev
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLUCID_REQUIRE_NATIVE_VULKAN=ON
    cmake --build build --parallel
    ./build/lucidswitch2 --gpu-info
    ./build/lucidswitch2 --graphics-demo --vulkan
    ./build/lucidswitch2 --graphics-demo --window --vulkan

Use --graphics-demo --window --vulkan from an interactive desktop session. On headless systems, --graphics-demo --vulkan performs native off-screen rendering and readback. --graphics-demo without --vulkan uses the native path automatically when it was compiled, otherwise the software reference path.

## Guest command stream

A command header is one little-endian uint32: opcode in bits 31..24, payload word count in bits 23..0.

| Opcode | Value | Payload |
|---|---:|---|
| NOP | 0x00 | none |
| BEGIN_FRAME | 0x01 | width, height |
| CLEAR | 0x02 | packed RGBA color |
| DRAW_RECT | 0x03 | x, y, width, height, packed RGBA color |
| PRESENT | 0x04 | none |
| CREATE_TEXTURE | 0x05 | ID, width, height, pixel-address low, pixel-address high, sampler flags |
| DESTROY_TEXTURE | 0x06 | ID |
| DRAW_INDEXED | 0x07 | vertex-address low/high, vertex count, index-address low/high, index count, texture ID (0 = untextured) |
| CLEAR_DEPTH | 0x08 | IEEE-754 float depth bits |
| END | 0xFF | none |

CREATE_TEXTURE sampler flags use bit 0 for linear filtering, bit 1 for U repeat, and bit 2 for V repeat. Unknown bits are rejected.

Indexed vertex data contains six little-endian uint32 words per vertex: X float, Y float, Z float, packed RGBA, U float, V float. Index data is a little-endian array of uint32 indices. The command processor validates buffer ranges, command ordering, vertex/index counts, texture references, depth values, and index bounds before dispatch.

## Deliberate limits

- This is not a Switch 2 GPU implementation and does not interpret console GPU command formats.
- The Vulkan backend executes only the project's original two-shader pipeline; it does not translate guest shaders or run arbitrary shader programs.
- The vertex/index upload buffers are host-visible coherent Vulkan buffers read by the GPU. Texture and render-target images are device-local. A future optimization can add persistent device-local geometry pools and staging/ring allocators.
- The current native renderer is synchronous: it waits for queue completion on each texture upload and presented frame. It does not yet use multiple frames in flight or a fence-driven asynchronous scheduler.
- The window renderer uses an off-screen color/depth target and blits to a swapchain transfer-destination image. A surface must support transfer-destination usage and the selected blit format.
- There is no MSAA, alpha blending, stencil operations, mipmapping, compressed/tiled guest texture support, render-target sampling, compute pipeline, shader cache, or console-specific cache/synchronization model.
- hardware_rasterization reports whether the selected physical device is discrete/integrated; a software Vulkan implementation can still execute the native Vulkan pipeline, but it is not hardware accelerated.
- SDL2 swapchain presentation requires a working desktop display and suitable Vulkan surface support. The off-screen path works without a window.

References: Khronos Vulkan graphics pipeline (https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/02_Graphics_pipeline_basics/00_Introduction.html), Khronos swapchain overview (https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/01_Presentation/01_Swap_chain.html), and SDL2 Vulkan surface creation (https://wiki.libsdl.org/SDL2/SDL_Vulkan_CreateSurface).


## GPU virtual-address space groundwork

The generic GPU address-space layer maps 4 KiB-aligned GPU virtual ranges onto checked shared physical memory. Mappings enforce read/write/execute permissions, can be protected or unmapped by exact region, support adjacent ranges for cross-page accesses, and alias the same backing physical bytes when mapped to the same address. `GpuCommandProcessor` consumes a read-only `GpuMemoryView`, so command, vertex, index, and texture buffers can be read from either a flat test allocation or the sparse mapped address space.

This is generic MMU groundwork only. It does not implement the Tegra GPU MMU page-table format, GPU page compression, swizzling/tiling, sparse residency, page faults, DMA engines, cache operators, GPU VMID/ASID behavior, or Switch 2 Ampere command channels.
