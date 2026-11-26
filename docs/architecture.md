# Architecture Notes

## Scope

LucidSwitch2 is an early clean-room emulator research project. Its host-side core maps permission-aware synthetic memory, executes a small A64 instruction subset, and runs deterministic CPU test programs. It does not reproduce a console hardware architecture or execute console software.

## Layers

- Application shell (src/main.cpp): command-line help, version, self-test, and synthetic CPU execution demo.
- Core session (lucid::core::Emulator): initializes guest memory, loads caller-supplied bytes, runs the CPU for a bounded instruction budget, supports resume/stop, and reports CPU faults.
- Memory bus (lucid::core::MemoryBus): checked host-side regions with read, write, and execute permissions.
- CPU interpreter (lucid::cpu::A64Interpreter): selected A64 instructions for synthetic programs, including translation-aware fetch/load/store and selected MRS/MSR encodings; see a64-subset.md.
- CPU memory facade (lucid::cpu::CpuMemory): optional virtual-to-physical translation and access-permission enforcement using the software AddressSpace map.
- Graphics contract (lucid::gpu::GraphicsBackend): future host rendering boundary only; no guest GPU command processor or renderer exists.

## Design constraints

1. Keep hardware assumptions out of generic utilities.
2. Base architecture-specific work on public documentation and independently written tests.
3. Make unsupported operations explicit; never pretend an instruction or GPU command succeeded.
4. Separate host rendering from guest command processing.
5. Keep firmware, keys, and commercial game data out of the repository and CI.
6. Add compatibility claims only with repeatable test evidence.
7. Bound CPU execution so test programs cannot run indefinitely inside the host application.

## Current execution flow

1. Caller initializes EmulatorConfig with a guest memory range and entry point.
2. Emulator maps that range, creates identity mappings for its guest pages, and initializes the A64 CPU context.
3. Caller loads a synthetic byte sequence into mapped memory.
4. run(instruction_budget) executes at most the requested number of instructions; CPU fetches and data accesses pass through the translation-aware memory facade.
5. Emulator pauses at the budget or enters Faulted on an unsupported instruction, system-register write fault, or access/translation fault.

The instruction budget is a host safety mechanism, not a guest timing model.

## Next engineering milestone

Build a proper decoder/test corpus and add broader, specification-backed A64 coverage. After CPU correctness work, add documented system-register, exception, MMU, memory-ordering, and device interfaces before console-specific work.


## Phase 2 components

See [phase-2-architecture.md](phase-2-architecture.md) for the system-register, exception, address-space, timer, interrupt, MMIO, and frontend prototype status. Translation-aware CPU memory access and selected MRS/MSR instructions are now integrated; exception routing, timers, interrupts, and MMIO remain separate building blocks.
