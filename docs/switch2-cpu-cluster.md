# Switch 2 CPU cluster foundation

## What is implemented

The Switch 2 target now has a bounded CPU-cluster abstraction with eight independent AArch64 architectural contexts. Each context keeps its own 31 general-purpose registers, 32 modeled SIMD/FP registers, stack pointer, program counter, condition flags, system-register bank, and retired-instruction count.

All eight contexts can share the same checked guest memory bus. Secondary cores begin parked and must be explicitly started with an aligned entry point. \`run_round_robin\` gives one instruction attempt to each runnable core in turn. An unsupported instruction or memory fault marks only that core faulted; the remaining runnable cores continue.

The smoke-test command is:

\`\`\`powershell
.\build\Release\lucidswitch2.exe --switch2-cpu-demo
\`\`\`

It runs a synthetic two-instruction program on each context and verifies independent register state and deterministic scheduling. It does not load game files.

## What is not implemented

This is not a complete Cortex-A78C CPU core. All contexts use the existing incomplete A64 interpreter. The experiment does not provide native host-thread execution, real simultaneous execution, exclusive monitors, atomic instructions, cache coherency, memory barriers, per-core TLBs/MMUs, interrupt routing, core affinity, power/frequency states, or the full Switch 2 exception and system-register architecture.

It is not integrated with the single-context \`core::Emulator\`, guest process runtime, or Horizon OS scheduler. The existing SVC dispatcher still reports virtual processor zero in its single-core execution path.

Most importantly, this work does not implement Switch 2 package loading, application startup, Horizon services, the Ampere GPU, shader translation, audio, input, display, or protected-content handling. No Switch 2 game is currently runnable or claimed compatible.

## Why this is a first step

Public community hardware research identifies the Switch 2 T239 as an eight-core Cortex-A78C system. Separate architectural contexts and shared guest memory are prerequisites for later multi-core execution, but they are only a small part of an emulator. Real game compatibility also requires a complete CPU instruction and memory-ordering model, a target loader, OS services, and an independent Ampere GPU implementation.
