# Architecture Notes

## Scope

The current code is a host-side foundation plus a deliberately limited A64 instruction interpreter. It does not reproduce a console hardware architecture and does not execute console software.

## Layers

- **Application shell** (`src/main.cpp`): command-line entry point and local self-test.
- **Core session** (`lucid::core::Emulator`): lifecycle and execution-backend availability checks.
- **Memory bus** (`lucid::core::MemoryBus`): checked host-side address-space regions for synthetic tests.
- **CPU interpreter** (`lucid::cpu::A64Interpreter`): selected A64 instructions for synthetic programs; see [a64-subset.md](a64-subset.md).
- **Graphics contract** (`lucid::gpu::GraphicsBackend`): future host rendering boundary.

## Design constraints

1. Keep hardware assumptions out of generic utilities.
2. Base architecture-specific work on public documentation and independently written tests.
3. Make unsupported operations explicit; never silently pretend an instruction or GPU command succeeded.
4. Separate host rendering from guest command processing.
5. Keep firmware, keys, and commercial game data out of the repository and CI.
6. Add compatibility claims only with repeatable test evidence.

## Next engineering milestone

Expand A64 coverage with flags, conditional branches, register arithmetic, and load/store instructions. Do not claim console compatibility from the existence of an instruction interpreter.
