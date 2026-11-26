# Known Limitations

This document is intentionally blunt so the project's maturity is not overstated.

- The A64 interpreter implements only the documented subset in a64-subset.md, not the complete ARMv8-A instruction set.
- The stage-1 MMU supports only one constrained 4 KiB / 48-bit TTBR0 regime with EL1 permission interpretation; it is not a complete ARMv8-A translation implementation.
- No TTBR1, ASIDs, additional translation granules, stage-2 translation, complete TLB maintenance instructions, memory attribute handling, or full architectural fault syndromes are implemented.
- MRS/MSR writes to TTBR0_EL1 and TCR_EL1 update the modeled register bank but do not yet reconfigure the active Stage1Mmu.
- Exception routing models selected EL1 state and vector slots only; there is no full exception entry/return, banked state, or privilege-transition model.
- Timer output, interrupt-controller signals, and MMIO device accesses are not yet connected to CPU execution.
- The guest GPU command processor accepts only the original documented Lucid test packet format; it is not a console GPU command processor.
- No guest operating system, kernel, or system-service implementation exists.
- No console-specific executable/package loader exists.
- No firmware, keys, game images, SDK files, leaked materials, or commercial assets are included or supported by this scaffold.
- The memory bus is a generic host-side physical backing store, not a map of any real console.
- The emulator execution loop runs synthetic programs loaded through its API; it is not a commercial game loader.
- A successful build, self-test, or CPU demo is not proof that console software can boot or run.
- No Nintendo Switch 2 hardware compatibility or commercial game compatibility is claimed.

## Graphics foundation and Phase 5

The guest GPU memory class is bounded host-backed storage, not a hardware memory controller. The original command processor supports begin-frame, clear, filled rectangle, present, create/destroy texture, indexed draws, and depth clear. The CPU reference backend implements deterministic triangle rasterization, depth testing, and texture sampling. See gpu-foundation.md.

## Phase 7 native Vulkan graphics

- The native backend requires Vulkan development files and glslc at configure/build time. Other builds use the software reference backend.
- Only the project's original vertex/fragment shader pair is supported. There is no guest shader translator, arbitrary pipeline state, or console graphics API implementation.
- Vertex and index data are uploaded into host-visible coherent Vulkan buffers. Texture and render-target images are device-local.
- The native frame path uses synchronous queue-idle waits. It does not yet implement frames in flight, asynchronous upload rings, or a fence-driven render scheduler.
- SDL2 presentation requires a desktop Vulkan surface. The implementation renders to an off-screen color/depth target and blits into a swapchain image; a surface must support transfer-destination use and the selected blit format.
- No MSAA, alpha blending, stencil, mipmaps, compressed/tiled guest textures, render-target sampling, compute, multi-pass effects, or shader caching are implemented.
- A software Vulkan device such as Mesa lavapipe executes the Vulkan pipeline but does not provide hardware GPU acceleration. The backend reports device class separately from native shader-pipeline availability.

## Phase 6 homebrew environment

- LXP is an original synthetic test package, not a Nintendo, NSO, NSP, XCI, ELF, or other commercial console executable format.
- Only a 48-byte LXP v1 header, raw code/data payload, bounded zero-filled BSS, and a reserved stack are supported.
- No relocations, dynamic linking, imported functions, syscalls, filesystem access, threads, GPU APIs, guest OS, or system services are implemented.
- The runner executes only the documented A64 subset and pauses at a host instruction budget; it does not provide a program exit syscall or commercial-game compatibility.
- CRC-32 detects accidental payload corruption but does not authenticate a package or prove its origin.
