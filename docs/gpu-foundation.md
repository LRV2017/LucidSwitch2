# Graphics pipeline foundation

Phase 5 adds a clean-room graphics pipeline above the Phase 4 guest GPU command path.

## Resource and pipeline model

- `GraphicsVertex` contains six 32-bit words: pixel-space X/Y, normalized depth Z, packed `0xRRGGBBAA` color, and normalized U/V texture coordinates.
- Vertex and index data are read from bounded guest GPU memory. Indices are uint32 values and each draw is a triangle list.
- A render target contains an RGBA8 color attachment and a floating-point depth attachment. `begin_frame` opens the render pass and creates/resizes the framebuffer; `present_frame` closes and publishes it.
- Depth clear is explicit, and indexed triangles use a less-or-equal depth test.
- Textures have stable nonzero IDs and explicit create/destroy operations. Samplers support nearest or linear filtering and clamp or repeat address modes.
- The software backend implements a deterministic triangle rasterizer with interpolated vertex color, UV and depth, nearest/bilinear texture sampling, and color/depth readback.

## Guest command packets

A command header is one little-endian `uint32`: opcode in bits 31..24, payload word count in bits 23..0.

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

`CREATE_TEXTURE` sampler flags use bit 0 for linear filtering, bit 1 for U repeat, and bit 2 for V repeat. Unknown bits are rejected.

Indexed vertex data contains six little-endian uint32 words per vertex: X float, Y float, Z float, packed RGBA, U float, V float. Index data is a little-endian array of uint32 indices. The command processor validates buffer ranges, command ordering, vertex/index counts, texture references, depth values, and index bounds before dispatch.

## Renderer and desktop presentation

- `SoftwareBackend` is the deterministic reference renderer for the supported command subset and supports framebuffer readback for tests.
- `VulkanBackend` can initialize a Vulkan device and upload completed RGBA8 frames through a staging buffer into a device-local image. It currently delegates rasterization to the software backend; `hardware_rasterization` is intentionally reported as false. Uploads are synchronous and wait for queue completion before temporary resources are released.
- `WindowPresenter` uses optional SDL2 to display completed readback pixels in a desktop window, with accelerated SDL renderer selection and a software fallback. It is a window preview/presenter, not a Vulkan surface or swapchain.

## Run the original test scene

```powershell
.\build\Release\lucidswitch2.exe --graphics-demo
.\build\Release\lucidswitch2.exe --graphics-demo --window
```

The window option requires SDL2 development files at configure time and an active desktop session.

## Limitations

This is not a Switch 2 GPU implementation. It does not support console command formats, hardware shader translation, a native Vulkan graphics pipeline, a Vulkan swapchain, MSAA, blending, stencil, compute, tiled/compressed guest textures, asynchronous frames in flight, or console-specific cache/synchronization behavior. The current pipeline provides a deterministic test contract before native GPU execution is added.
