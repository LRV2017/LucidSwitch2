# LucidSwitch2

An experimental, clean-room emulator research project focused on building a lawful and testable foundation for studying modern console software and hardware interfaces.

> Status: early development. LucidSwitch2 has a bounded CPU execution loop, a permission-aware synthetic memory bus, and a small A64 interpreter. It does not emulate Nintendo Switch 2 hardware, load commercial Switch 2 games, or provide playable compatibility.

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
- Small A64 interpreter subset: NOP, MOVZ, MOVK, immediate and shifted-register ADD/SUB, selected logical operations, NZCV updates, common branches, and unsigned-offset scalar LDR/STR.
- Synthetic instruction and emulator integration tests.

The interpreter is an educational foundation, not a complete ARMv8-A CPU. See docs/a64-subset.md and docs/known-limitations.md.

## Build

Requirements:
- CMake 3.20 or newer
- A C++20 compiler (Visual Studio 2022 17.4+, Clang 15+, or GCC 12+)

### Windows (PowerShell)

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\lucidswitch2.exe --self-test
.\build\Release\lucidswitch2.exe --run
```

### Linux / macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/lucidswitch2 --self-test
./build/lucidswitch2 --run
```

The --run command executes a tiny synthetic branch-loop demo for a bounded number of instructions. It does not load a game.

## Frontend prototype\n\nOpen `frontend/index.html` in a modern browser to preview the console-inspired home screen, game library, settings, local metadata-only import flow, and basic controller detection. This is not yet a native desktop application and cannot launch imported files.\n\n## Phase 2 architecture components\n\nThe repository includes initial standalone models for selected system registers and EL1 exception vector routing, explicit 4 KiB address translation, a deterministic virtual timer, a 64-source interrupt controller, and an MMIO device bus. These are building blocks, not a complete ARMv8 implementation or fully integrated console hardware model. See `docs/phase-2-architecture.md`.\n\n## Roadmap

1. Host foundation: build system, memory primitives, CPU execution loop, lifecycle, diagnostics, and CI.
2. CPU research: validate more A64 instruction groups using public specifications and differential test vectors.
3. Architecture research: document publicly observable hardware requirements and define testable interfaces.
4. System emulation: add documented exceptions, system registers, memory translation, timers, interrupts, and device abstractions.
5. Graphics research: design a backend around public graphics standards, then validate with original test scenes.
6. Homebrew test environment: define a documented, original test format and sample programs with no proprietary code or assets.
7. Compatibility work: only after the above, test legally obtained user-provided software and publish reproducible results.

## Contributing

Please open an issue before a large change. Include the subsystem, public specification or original test, build/test commands, and known limitations. Do not submit firmware, keys, game dumps, leaked SDKs, or proprietary source/assets.

## Legal and naming

LucidSwitch2 is an independent research project and is not affiliated with, endorsed by, or sponsored by Nintendo. Nintendo Switch and related marks belong to their respective owner. This repository does not distribute Nintendo software, firmware, keys, or games. Users are responsible for complying with applicable laws and the terms governing any files they provide.

## License

The source code is licensed under the MIT License. See LICENSE.
