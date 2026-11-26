# LucidSwitch2

An experimental, clean-room emulator research project focused on building a lawful and testable foundation for studying modern console software and hardware interfaces.

> Status: early development. LucidSwitch2 has a bounded A64 execution loop, a permission-aware memory bus, a limited stage-1 MMU descriptor walker with a TLB, selected exception/device models, a reference graphics renderer, and an original homebrew test-program container. It does not emulate Nintendo Switch 2 hardware, load commercial Switch 2 games, or provide playable compatibility.

## Project principles

- Clean-room implementation: original code and independently authored documentation only.
- No bundled copyrighted material: no console firmware, encryption keys, game images, SDK files, leaked materials, or proprietary assets.
- User-supplied files stay local: future support must require users to provide lawfully obtained files themselves.
- Small, testable steps: each subsystem should have isolated tests and documented evidence before compatibility is claimed.
- No compatibility claims without proof: an interface is not evidence of hardware support.

## Current foundation

- C++20 / CMake project with cross-platform CI for Windows, Linux, and macOS.
- Checked memory regions with read, write, and execute permissions.
- CPU context with 31 general-purpose registers, SP, PC, retired-instruction count, and NZCV flags.
- Bounded emulator execution loop with program loading, pause-on-budget, resume, stop, and explicit CPU fault reporting.
- Small A64 interpreter subset: NOP, MOVZ, MOVK, immediate and shifted-register ADD/SUB, selected logical operations, NZCV updates, common branches, unsigned-offset scalar LDR/STR, and selected MRS/MSR system-register accesses.
- Translation-aware CPU memory facade: instruction fetches and data accesses pass through the stage-1 walker and TLB; emulator sessions build a synthetic identity map using L1 1 GiB block descriptors.
- Limited ARM stage-1 support: 4 KiB granule, 48-bit lower VA / 48-bit PA, TTBR0_EL1, L0-L3 walks, L1/L2 blocks, L3 pages, access-flag and EL1 permission checks, and explicit TLB invalidation.
- Limited exception model: EL0/EL1 state, SVC entry, EL1 vector routing, SPSR_EL1/ELR_EL1 save-and-restore, ERET, and deterministic timer-driven IRQ delivery.
- Graphics pipeline foundation: indexed vertex/index buffers, triangle rasterization, color/depth attachments, texture resources with nearest/linear sampling, validated guest GPU commands, optional Vulkan off-screen uploads, and SDL2 desktop frame presentation.
- Phase 6 homebrew test environment: a bounded LXP package reader/writer, metadata inspection, CRC-32 validation, safe segment sizing, an instruction-budgeted runner, and an original A64 register demo.

The LXP format is an original synthetic test container. It is not a Nintendo executable format and does not implement a guest operating system, syscalls, dynamic linking, or console services. See [docs/homebrew-format.md](docs/homebrew-format.md).

The graphics path is a clean-room renderer foundation, not Switch 2 GPU emulation. Indexed triangles, depth testing, and texture sampling are currently rasterized by the CPU reference backend. The optional Vulkan backend uploads completed frames to a device-local image but does not yet rasterize with a native Vulkan shader pipeline or swapchain. SDL2 can display frame readbacks in a desktop window. See [docs/gpu-foundation.md](docs/gpu-foundation.md).

The exception path is deliberately limited: it models selected SVC and IRQ entry to EL1 plus ERET, but it is not a complete PSTATE, exception-syndrome, or interrupt-controller implementation.

The interpreter and MMU are educational foundations, not a complete ARMv8-A implementation. The walker supports only one documented 4 KiB / 48-bit TTBR0 regime and does not implement the full architectural permission, fault, cacheability, ASID, or TLB maintenance model. See [docs/a64-subset.md](docs/a64-subset.md), [docs/mmu-stage1.md](docs/mmu-stage1.md), and [docs/known-limitations.md](docs/known-limitations.md).

## Build

Requirements:
- CMake 3.20 or newer
- A C++20 compiler (Visual Studio 2022 17.4+, Clang 15+, or GCC 12+)
- Optional: Vulkan SDK for off-screen device uploads and SDL2 for the desktop preview window; the software renderer and LXP loader work without either

### Windows (PowerShell)

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
    ctest --test-dir build -C Release --output-on-failure
    .\build\Release\lucidswitch2.exe --self-test
    .\build\Release\lucidswitch2.exe --run
    .\build\Release\lucidswitch2.exe --gpu-info
    .\build\Release\lucidswitch2.exe --graphics-demo
    .\build\Release\lucidswitch2.exe --graphics-demo --window
    .\build\Release\lucidswitch2.exe --create-homebrew-sample .\build\register_demo.lxp
    .\build\Release\lucidswitch2.exe --inspect-homebrew .\build\register_demo.lxp
    .\build\Release\lucidswitch2.exe --run-homebrew .\build\register_demo.lxp 3

### Linux / macOS

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure
    ./build/lucidswitch2 --self-test
    ./build/lucidswitch2 --run
    ./build/lucidswitch2 --gpu-info
    ./build/lucidswitch2 --graphics-demo
    ./build/lucidswitch2 --graphics-demo --window
    ./build/lucidswitch2 --create-homebrew-sample ./build/register_demo.lxp
    ./build/lucidswitch2 --inspect-homebrew ./build/register_demo.lxp
    ./build/lucidswitch2 --run-homebrew ./build/register_demo.lxp 3

The included register demo sets X0 to 42, sets X1 to 0xC0DE, and then branches to itself. With a budget of three instructions the host pauses execution and prints the register state. The default homebrew run budget is 100,000 instructions; the maximum accepted CLI budget is 10,000,000.

## Frontend prototype

Open frontend/index.html in a modern browser to preview the console-inspired home screen, game library, settings, local metadata-only import flow, and basic controller detection. This is not yet a native desktop application and cannot launch imported files.

## Roadmap

1. Host foundation: build system, memory primitives, CPU execution loop, lifecycle, diagnostics, and CI.
2. CPU research: validate more A64 instruction groups using public specifications and differential test vectors.
3. Architecture research: document publicly observable hardware requirements and define testable interfaces.
4. System emulation: add documented exceptions, system registers, memory translation, timers, interrupts, and device abstractions.
5. Graphics foundation: indexed geometry, depth framebuffer, texture and sampler resources, guest command buffers, and optional desktop preview.
6. Homebrew test environment: original LXP package format, bounded loading/execution, sample programs, and malformed-package tests.
7. Native graphics execution: native Vulkan shader pipeline, GPU-side geometry/textures, and surface/swapchain presentation.
8. Compatibility research: only after the above, investigate legally obtained user-provided software and publish reproducible results.

Phase 6 does not make the project a console emulator. It creates a reproducible, original program format for exercising the CPU and loader foundation.

## Contributing

Please open an issue before a large change. Include the subsystem, public specification or original test, build/test commands, and known limitations. Do not submit firmware, keys, game dumps, leaked SDKs, or proprietary source/assets.

## Legal and naming

LucidSwitch2 is an independent research project and is not affiliated with, endorsed by, or sponsored by Nintendo. Nintendo Switch and related marks belong to their respective owner. This repository does not distribute Nintendo software, firmware, keys, or games. Users are responsible for complying with applicable laws and the terms governing any files they provide.

## License

The source code is licensed under the MIT License. See LICENSE.
