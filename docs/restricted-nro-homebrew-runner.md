# Restricted NRO homebrew runner

## Purpose

The Phase 9 NRO runner is a small, bounded execution experiment for a narrow class of original-Switch homebrew binaries. It is not a Switch game launcher and is not expected to run typical libnx applications yet.

## Command

Windows:

    .\build\Release\lucidswitch2.exe --run-nro .\minimal-homebrew.nro 1000

Linux/macOS:

    ./build/lucidswitch2 --run-nro ./minimal-homebrew.nro 1000

The instruction budget is optional (default 100,000) and is capped at 10,000,000.

## Accepted profile

- Plaintext NRO0 with a zero flags field only; NSO0 remains metadata-only.
- NRO compression variants are not supported.
- NRO-start offset 0 must contain a direct A64 B-immediate instruction.
- The branch target must be aligned and fall inside the validated .text segment.
- The MOD0 pointer must be zero. Nonzero MOD0 metadata is rejected because the loader does not yet perform dynamic relocations or linking.
- The image, zero-filled BSS, and a 64 KiB stack are placed in a bounded guest memory region.
- Execution uses the project's small A64 instruction subset and stops at a host instruction budget.

## ABI and missing runtime

The documented NRO Homebrew ABI places the entry point at binary_ptr + 0, where the branch stub jumps past the NRO0 header. NRO applications receive a loader-config pointer in X0 and 0xFFFFFFFFFFFFFFFF in X1. This prototype passes a null loader-config pointer and the documented X1 sentinel. No loader-config entries or handles are provided.

The runtime does not implement:
- MOD0 processing, relocations, dynamic imports, module loading, or symbol resolution;
- complete CPU instruction coverage, SIMD/FP, atomics, or architectural exception behavior;
- SVC implementations, process/thread creation, scheduling, handles, TLS, or synchronization;
- HIPC/CMIF, service-manager registration, filesystem, input, audio, display, or application lifecycle;
- original Switch or Switch 2 guest GPU commands, shaders, or compositor.

Typical Homebrew applications depend on runtime services and CPU instructions outside this prototype. They will generally fault or stop at the first unsupported requirement. Reaching the instruction budget means only that the current bounded test image executed that many instructions.

## Importing software

The command accepts a user-supplied local .nro path. It does not download software or import keys. The project currently does not accept .nsp, .xci, .nca, production keys, or title keys. Switch 2 software is not supported by this runner.

Only use homebrew/test binaries you are authorized to run. This prototype is intended for independently built test programs until its loader and OS runtime mature.
