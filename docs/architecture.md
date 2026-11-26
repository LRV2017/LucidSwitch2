# Architecture Notes

## Scope

LucidSwitch2 is an early clean-room emulator research project. Its host-side core maps permission-aware synthetic memory, executes a small A64 instruction subset, runs deterministic CPU test programs, and provides original graphics and homebrew test interfaces. It does not reproduce a console hardware architecture or execute console software.

## Layers

- Application shell (src/main.cpp): command-line help, version, self-test, graphics demo, LXP package creation/inspection, and bounded synthetic program execution.
- Homebrew package layer (lucid::homebrew): strict LXP v1 serialization, CRC-32 verification, segment bounds and file-size validation, and file I/O.
- Core session (lucid::core::Emulator): initializes guest memory, loads caller-supplied bytes, runs the CPU for a bounded instruction budget, supports resume/stop, and reports CPU faults.
- Memory bus (lucid::core::MemoryBus): checked host-side regions with read, write, and execute permissions.
- CPU interpreter (lucid::cpu::A64Interpreter): selected A64 instructions for synthetic programs, including translation-aware fetch/load/store and selected MRS/MSR encodings; see a64-subset.md.
- CPU memory facade (lucid::cpu::CpuMemory): virtual-to-physical translation and access-permission enforcement using either the standalone AddressSpace map or the stage-1 MMU.
- Stage-1 MMU (lucid::mmu::Stage1Mmu): bounded 4 KiB / 48-bit TTBR0 descriptor walker with L1/L2 block support and a fixed-size software TLB.
- Graphics contract (lucid::gpu::GraphicsBackend): original host renderer boundary with a CPU reference rasterizer and an optional Vulkan image-upload backend; not console GPU emulation.
- Desktop presenter (lucid::gpu::WindowPresenter): optional SDL2 preview for host framebuffer readbacks.

## Design constraints

1. Keep hardware assumptions out of generic utilities.
2. Base architecture-specific work on public documentation and independently written tests.
3. Make unsupported operations explicit; never pretend an instruction or GPU command succeeded.
4. Separate host rendering from guest command processing.
5. Keep firmware, keys, and commercial game data out of the repository and CI.
6. Add compatibility claims only with repeatable test evidence.
7. Bound CPU execution and package sizes so test programs cannot run indefinitely or allocate unbounded memory inside the host application.
8. Validate a complete package header, segment layout, and checksum before initializing a guest session.

## Current LXP execution flow

1. Caller creates an original LXP v1 file or supplies one independently.
2. The package reader checks magic, version, header size, reserved flags, exact file/payload lengths, CRC-32, address alignment, entry point, and bounded segment sizes.
3. The runner creates a synthetic guest memory range, copies code and initialized data, relies on zero-filled guest RAM for BSS, and initializes the stack pointer.
4. The A64 interpreter runs at most the user-selected instruction budget (default 100,000; CLI maximum 10,000,000).
5. Budget exhaustion pauses the session. Unsupported instructions and memory faults are surfaced as CPU faults; no guest syscalls or operating-system services are emulated.

## Synthetic CPU execution flow

1. Caller initializes EmulatorConfig with a guest memory range and entry point.
2. Emulator maps that range, allocates translation-table memory, creates L0/L1 identity descriptors for the 1 GiB blocks covering guest RAM, and initializes TTBR0_EL1/TCR_EL1 plus the A64 context.
3. Caller loads a synthetic byte sequence into mapped memory.
4. run(instruction_budget) executes at most the requested number of instructions; CPU fetches and data accesses pass through the stage-1 descriptor walker and TLB.
5. Emulator pauses at the budget or enters Faulted on an unsupported instruction, system-register write fault, or access/translation fault.

The instruction budget is a host safety mechanism, not a guest timing model.

## Next engineering milestone

Build a proper decoder/test corpus and add broader, specification-backed A64 coverage. Continue to document system-register, exception, MMU, memory-ordering, and device behavior before console-specific work.
