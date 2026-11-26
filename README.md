# LucidSwitch2

An experimental, clean-room emulator research project focused on building a lawful and testable foundation for studying modern console software and hardware interfaces.

> Status: early development. LucidSwitch2 has a bounded A64 execution loop, a permission-aware memory bus, a limited stage-1 MMU descriptor walker with a TLB, selected exception/device models, a CPU reference renderer, an optional native Vulkan shader pipeline, and an original homebrew test-program container. It does not emulate Nintendo Switch 2 hardware, load commercial Switch 2 games, or provide playable compatibility.

## Project principles

- Clean-room implementation: original code and independently authored documentation only.
- No bundled copyrighted material: no console firmware, encryption keys, game images, SDK files, leaked materials, or proprietary assets.
- User-supplied files stay local: future support must require users to provide lawfully obtained files themselves.
- Small, testable steps: each subsystem should have isolated tests and documented evidence before compatibility is claimed.
- No compatibility claims without proof: an interface is not evidence of hardware support.

## Current foundation

- C++20 / CMake project with cross-platform CI for Windows, Linux, and macOS.
- Checked memory regions with read, write, and execute permissions.
- CPU context with 31 general-purpose registers, 32 modeled 128-bit SIMD/FP registers, SP, PC, retired-instruction count, and NZCV flags.
- Bounded emulator execution loop with program loading, pause-on-budget, resume, stop, and explicit CPU fault reporting.
- Expanded but incomplete A64 interpreter: MOVN/MOVZ/MOVK, ADR/ADRP, logical immediates, conditional selects, UDIV/SDIV, variable shifts, MADD/MSUB, linked branches, scalar S/D FMOV and FADD/FSUB/FMUL/FDIV, NZCV updates, common branches, unsigned-offset scalar LDR/STR, and selected MRS/MSR system-register accesses.
- Translation-aware CPU memory facade: instruction fetches and data accesses pass through the stage-1 walker and TLB; emulator sessions build a synthetic identity map using L1 1 GiB block descriptors.
- Limited ARM stage-1 support: 4 KiB granule, 48-bit lower VA / 48-bit PA, TTBR0_EL1, L0-L3 walks, L1/L2 blocks, L3 pages, access-flag and EL1 permission checks, and explicit TLB invalidation.
- Limited exception model: EL0/EL1 state, SVC entry, EL1 vector routing, SPSR_EL1/ELR_EL1 save-and-restore, ERET, and deterministic timer-driven IRQ delivery.
- Graphics pipeline foundation: indexed geometry, triangle rasterization, color/depth attachments, texture resources with nearest/linear sampling, and validated guest GPU commands.
- Phase 7 native graphics execution: an optional Vulkan vertex/fragment pipeline with GPU vertex/index buffers, device-local sampled texture images, descriptor sets, depth-tested rendering, off-screen readback, and an SDL2 Vulkan swapchain presentation path.
- Phase 6 homebrew test environment: a bounded LXP package reader/writer, metadata inspection, CRC-32 validation, safe segment sizing, an instruction-budgeted runner, and an original A64 register demo.
- Phase 8 compatibility research foundation: separate original-Switch and Switch 2 architecture profiles, a read-only plaintext NRO0/NSO0 metadata inspector, and target-specific compatibility-gap reports.
- Phase 9 restricted homebrew execution prototype: a bounded loader for a deliberately small subset of plaintext, uncompressed original-Switch NRO files with a direct entry branch and no MOD0/dynamic-linking metadata.
- NSP package import: a read-only PFS0 directory parser that validates package metadata and lists NCA, ticket, certificate, and other entries without reading/decrypting NCA content.
- Phase 10 initial CPU expansion: 32 modeled 128-bit FP/SIMD registers and scalar single/double FMOV, FADD, FSUB, FMUL, and FDIV instructions; advanced vector NEON and FP control remain unsupported.
- Phase 11 CPU expansion: MOVN, ADR/ADRP, logical-immediate decoding, CSEL-family instructions, integer division, variable shifts, multiply-accumulate, and link-register branches. This remains a partial integer core.

The LXP format is an original synthetic test container. It is not a Nintendo executable format and does not implement a guest operating system, syscalls, dynamic linking, or console services. See [docs/homebrew-format.md](docs/homebrew-format.md).

LucidSwitch2 accepts `.nsp` packages for read-only PFS0 directory inspection through `--inspect-nsp`. The library UI also accepts `.nsp` files, validates the PFS0 file table and data ranges locally, and summarizes package entries. Phase 9 adds a very restricted original-Switch NRO execution prototype, but only for a direct branch-to-text image with no MOD0 header/dynamic relocations. It uses the incomplete A64 interpreter and has no Horizon OS, syscalls, imports, threads, or console GPU, so ordinary homebrew applications and commercial games are not expected to run. It does not process NCA containers or protected content. Switch 2 remains a separate research target with no executable loader. See [docs/switch-architecture.md](docs/switch-architecture.md), [docs/switch-executable-inspection.md](docs/switch-executable-inspection.md), [docs/restricted-nro-homebrew-runner.md](docs/restricted-nro-homebrew-runner.md), and [docs/nsp-package-import.md](docs/nsp-package-import.md). Research-source notes, including the supplied ROMSLAB link, are in [docs/research-sources.md](docs/research-sources.md).

The graphics path is a clean-room renderer foundation, not Switch 2 GPU emulation. When Vulkan and glslc are available, the native backend executes the project's own vertex and fragment shaders, consumes Vulkan vertex/index buffers, samples device-local texture images, performs depth-tested rendering, and reads back the completed off-screen image. With SDL2, it can present by blitting the GPU target into a real Vulkan swapchain. The CPU reference renderer remains the portable fallback. The Vulkan path currently uses synchronous queue waits and a single fixed shader pair; it does not translate console shaders or implement console GPU commands. See [docs/gpu-foundation.md](docs/gpu-foundation.md).

The exception path is deliberately limited: it models selected SVC and IRQ entry to EL1 plus ERET, but it is not a complete PSTATE, exception-syndrome, or interrupt-controller implementation.

The interpreter and MMU are educational foundations, not a complete ARMv8-A implementation. The walker supports only one documented 4 KiB / 48-bit TTBR0 regime and does not implement the full architectural permission, fault, cacheability, ASID, or TLB maintenance model. See [docs/a64-subset.md](docs/a64-subset.md), [docs/mmu-stage1.md](docs/mmu-stage1.md), and [docs/known-limitations.md](docs/known-limitations.md).

## Build

Requirements:
- CMake 3.20 or newer
- A C++20 compiler (Visual Studio 2022 17.4+, Clang 15+, or GCC 12+)
- Optional: Vulkan SDK plus glslc for the native GPU pipeline, and SDL2 development files for Vulkan swapchain presentation; the software renderer and LXP loader work without these tools

### Windows (PowerShell)

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
    ctest --test-dir build -C Release --output-on-failure
    .\build\Release\lucidswitch2.exe --self-test
    .\build\Release\lucidswitch2.exe --run
    .\build\Release\lucidswitch2.exe --gpu-info
    .\build\Release\lucidswitch2.exe --graphics-demo
    .\build\Release\lucidswitch2.exe --graphics-demo --window
    .\build\Release\lucidswitch2.exe --graphics-demo --vulkan
    .\build\Release\lucidswitch2.exe --graphics-demo --window --vulkan
    .\build\Release\lucidswitch2.exe --create-homebrew-sample .\build\register_demo.lxp
    .\build\Release\lucidswitch2.exe --compat-report all
    .\build\Release\lucidswitch2.exe --inspect-nsp .\your-game.nsp
    .\build\Release\lucidswitch2.exe --inspect-switch-executable .\homebrew.nro
    .\build\Release\lucidswitch2.exe --run-nro .\minimal-homebrew.nro 1000
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
    ./build/lucidswitch2 --graphics-demo --vulkan
    ./build/lucidswitch2 --graphics-demo --window --vulkan
    ./build/lucidswitch2 --create-homebrew-sample ./build/register_demo.lxp
    ./build/lucidswitch2 --compat-report all
    ./build/lucidswitch2 --inspect-nsp ./your-game.nsp
    ./build/lucidswitch2 --inspect-switch-executable ./homebrew.nro
    ./build/lucidswitch2 --run-nro ./minimal-homebrew.nro 1000
    ./build/lucidswitch2 --inspect-homebrew ./build/register_demo.lxp
    ./build/lucidswitch2 --run-homebrew ./build/register_demo.lxp 3

The included register demo sets X0 to 42, sets X1 to 0xC0DE, and then branches to itself. With a budget of three instructions the host pauses execution and prints the register state. The default homebrew run budget is 100,000 instructions; the maximum accepted CLI budget is 10,000,000.

## Frontend prototype

Open frontend/index.html in a modern browser to preview the console-inspired home screen, game library, settings, local metadata-only import flow, and basic controller detection. This is not yet a native desktop application and cannot launch imported files.

## Roadmap

Phases 1-8 established the host and research foundation. Phase 9 adds a restricted NRO execution experiment only. General original-Switch homebrew and Switch 2 software still require substantial target-specific engineering.

1. Host foundation: build system, memory primitives, CPU execution loop, lifecycle, diagnostics, and CI.
2. CPU research: validate A64 instruction groups using public specifications and differential test vectors.
3. Architecture research: document hardware requirements and define testable interfaces.
4. System foundation: selected exceptions, system registers, memory translation, timers, interrupts, and device abstractions.
5. Graphics foundation: indexed geometry, depth framebuffer, textures, guest test commands, and desktop preview.
6. Homebrew test environment: original LXP format, bounded loading/execution, sample programs, and malformed-package tests.
7. Native graphics execution: native Vulkan shader pipeline, GPU-side geometry/textures, off-screen readback, and optional SDL2 swapchain presentation.
8. Compatibility research baseline: separate original-Switch and Switch 2 target profiles, plaintext NRO0/NSO0 metadata inspection, architecture documentation, and explicit compatibility-gap reports.
9. Homebrew image loading: initial restricted NRO execution proof for a direct branch-to-text, no-MOD0 image using the bounded A64 subset. Full segment permissions, MOD0 relocation handling, and the standard homebrew runtime remain future work.
10. CPU completion work: expand scalar FP precision/control, implement advanced SIMD/NEON, atomics/exclusives, broader exception handling, and target-specific CPU extensions.
11. Kernel/process model: address-space management, SVC ABI, process/thread state, scheduler, handles, TLS, synchronization, and timers.
12. Horizon IPC and services: HIPC/CMIF framing, sessions/domains, service-manager behavior, and selected documented homebrew-facing services.
13. Platform services: filesystem, content access, input, audio, display, and application lifecycle.
14. Original Switch graphics emulation: research and implement a separate guest GPU command processor, shader translation, memory layouts, and synchronization path.
15. Switch 2 architecture and graphics: implement and validate Switch 2-specific CPU, memory, OS, and GPU behavior independently from the original Switch.
16. Compatibility laboratory: reproducible test corpus, homebrew test suites, per-title diagnostics, and performance profiling.

Later phases remain conditional on test evidence. No phase guarantees that every commercial title can be made compatible. Protected retail-content decryption and production/title-key handling are not part of this implementation.

Phase 8 does not make the project a console emulator. It establishes separate hardware profiles, an original-Switch executable metadata inspector, and a testable map of what still must be implemented for either target.

## Contributing

Please open an issue before a large change. Include the subsystem, public specification or original test, build/test commands, and known limitations. Do not submit firmware, keys, game dumps, leaked SDKs, or proprietary source/assets.

## Legal and naming

LucidSwitch2 is an independent research project and is not affiliated with, endorsed by, or sponsored by Nintendo. Nintendo Switch and related marks belong to their respective owner. This repository does not distribute Nintendo software, firmware, keys, or games. Users are responsible for complying with applicable laws and the terms governing any files they provide.

## License

The source code is licensed under the MIT License. See LICENSE.
