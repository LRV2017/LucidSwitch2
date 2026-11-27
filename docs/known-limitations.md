# Known Limitations

This document is intentionally blunt so the project's maturity is not overstated.

- The A64 interpreter implements only the documented subset in a64-subset.md, not the complete ARMv8-A instruction set. Scalar S/D FP support is limited to FMOV, FADD, FSUB, FMUL, and FDIV; Advanced SIMD is a small integer-only subset; exclusive memory support is limited to aligned 32/64-bit LDXR/LDAXR/STXR/STLXR and CLREX.
- The stage-1 MMU supports only one constrained 4 KiB / 48-bit TTBR0 regime with EL1 permission interpretation; it is not a complete ARMv8-A translation implementation.
- No TTBR1, ASIDs, additional translation granules, stage-2 translation, complete TLB maintenance instructions, memory attribute handling, or full architectural fault syndromes are implemented.
- MRS/MSR writes to TTBR0_EL1 and TCR_EL1 update the modeled register bank but do not yet reconfigure the active Stage1Mmu.
- Exception routing models selected EL1 state and vector slots only; there is no full exception entry/return, banked state, or privilege-transition model.
- Timer output, interrupt-controller signals, and MMIO device accesses are not yet connected to CPU execution.
- The guest GPU command processor accepts only the original documented Lucid test packet format; it is not a console GPU command processor.
- No guest operating system, kernel, or system-service implementation exists.
- Only a restricted minimal NRO0 homebrew execution prototype exists; it is not a general console executable/package loader.
- No firmware, keys, game images, SDK files, leaked materials, or commercial assets are included or supported by this scaffold.
- The memory bus is a generic host-side physical backing store, not a map of any real console.
- The emulator can run original LXP test programs and a narrow synthetic/minimal NRO profile. It is not a commercial game loader.
- A successful build, self-test, or CPU demo is not proof that console software can boot or run.
- No Nintendo Switch 2 hardware compatibility or commercial game compatibility is claimed.
- A separate Switch 2 CPU-cluster experiment has eight independent A64 contexts and deterministic round-robin stepping over shared guest memory. It is not integrated into the single-core `Emulator`, does not use real host parallelism, and does not model cache coherency, barriers, affinity, or Horizon scheduling. The guest memory bus now models conservative exclusive reservations for interleaved core execution, but is not safe for simultaneous host-thread access.

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


## Phase 8 compatibility research

- The NRO0/NSO0 inspector reads plaintext metadata only. The separate Phase 9 NRO runner accepts plaintext NRO0 with a direct entry branch and bounded relative MOD0 relocation support; it does not decompress, verify module hashes, perform symbol linking, import modules, or provide Horizon services.
- NRO inspection currently supports the classic uncompressed file-offset layout; compressed/extended NRO variants are rejected.
- NSO compression, hash-check, execute-only, and Zstd flags are reported, but the segment contents are not decompressed or hash-verified.
- No NCA content archive parser or encrypted content processing is implemented.
- No production keys or title keys are stored, retrieved, derived, or used.
- Original Switch and Switch 2 have distinct compatibility profiles. Parsing an original-Switch NRO0 or NSO0 header is not Switch 2 support.
- There is no Horizon OS process runtime, full SVC ABI, scheduler, HIPC/CMIF service stack, filesystem service, audio/input/display service set, or target GPU command processor.
- The Vulkan renderer is a host renderer for original LucidSwitch2 test geometry. It does not emulate Maxwell or Ampere guest GPU commands or shaders.
- No commercial Switch or Switch 2 game is currently loadable or claimed compatible.


## Phase 9 restricted NRO runner

- Only plaintext, uncompressed NRO0 with a zero flags field is accepted. NSO0 is metadata-only.
- The NRO-start branch must be an A64 B-immediate instruction that targets the validated .text segment.
- MOD0 parsing supports only bounded, symbol-free AArch64 RELATIVE/NONE RELA entries and RELR relative relocations; other relocation types are rejected.
- The runner uses a fixed 64 KiB stack, bounded guest memory, and a maximum 10,000,000-instruction budget.
- The emulated CPU supports only the documented A64 subset. Real homebrew commonly depends on many instructions and services not implemented here.
- The current implementation uses one coarse RWX guest RAM mapping; it does not yet enforce per-segment text/rodata/data page permissions.
- No loader configuration entries or general system-call ABI are provided; only SVC 0x10 and 0x1E are dispatched. The process/thread manager is a host-side substrate, not a guest Horizon runtime; HIPC/CMIF, filesystem, input, audio, display, and guest GPU are not integrated.
- An instruction-budget stop proves only that this small program ran for the requested number of instructions; it is not evidence of game compatibility.
- Protected NSP/XCI/NCA content and production/title-key decryption are not supported.


## NSP package import

- NSP import validates only the PFS0 directory metadata and the filenames/ranges of its entries.
- NCA, ticket, and certificate payloads are not read or extracted. The parser classifies them by filename and size only.
- NCA headers and protected sections are not decrypted or parsed; production/title keys are not imported or used.
- Every imported package remains marked not launchable. The runtime and guest systems required by a Switch title are not implemented.
- The frontend accepts `.nsp` files and persists a local summary only; it does not retain/upload package bytes.


## Phase 10 initial scalar floating-point support

- The CPU context models 32 128-bit SIMD/FP registers. It supports selected scalar S/D transfers/arithmetic and a small integer-vector subset (ADD/SUB and AND/ORR/EOR); this is not complete NEON.
- FMOV is bit-preserving for the supported general-register and scalar FP lane transfers; full SIMD lane semantics are not modeled.
- Scalar arithmetic uses host `float`/`double` operations. FPCR/FPSR, architectural rounding modes, exception flags and traps, flush-to-zero controls, and implementation-specific NaN behavior are not modeled.
- Advanced SIMD remains mostly unsupported: vector loads/stores, lane insert/extract, comparisons, shifts, permutations, saturation/halving, vector floating-point, table lookups, and crypto are not implemented.
- The limited scalar FP support does not materially establish commercial title compatibility; the CPU core still lacks many common A64 integer instructions, atomics/exclusives, and target-specific architectural behavior.


## Phase 11 AArch64 scalar integer expansion

- The interpreter adds MOVN, ADR/ADRP, logical immediates, CSEL/CSINC/CSINV/CSNEG, UDIV/SDIV, LSLV/LSRV/ASRV/RORV, MADD/MSUB, BL and BLR. This is still a small subset of ARMv8-A.
- Most Advanced SIMD/NEON vector instructions, atomic/exclusive memory operations, pointer authentication, broad load/store addressing modes, exception-syndrome completeness, and full system-register behavior remain unimplemented.
- These additional instructions are CPU groundwork; there is still no general NRO/NSO process loader, Horizon ABI, service manager, guest GPU, or commercial-game launch path.


## Guest kernel runtime substrate

- The new process/thread scheduler is deterministic and cooperative. It is not preemptive, has no timer quantum, and is not connected to automatic CPU execution.
- Kernel handles are process-local typed identifiers only; event, mutex, semaphore, session, and transfer-memory objects do not yet implement their guest-visible behavior.
- Address-space ownership, executable mapping, process launch parameters, TLS ABI, HIPC/CMIF integration, and Horizon services remain unimplemented. Only EL0 SVC 0x10 and 0x1E are dispatched.


## HIPC/CMIF framing foundation

- The parser supports only raw-data-only HIPC frames and CMIF Request/RequestWithContext messages. Pointer/static descriptors, send/receive/exchange buffers, PID, special headers, and copy/move handles are rejected.
- CMIF domain headers and domain object routing are not supported. The service registry is a host dispatch abstraction, not Horizon service-manager emulation.
- No real guest TLS command buffer, SVC IPC calls, HIPC session/handle lifecycle, TIPC, or named Horizon service implementation exists.


## NRO MOD0 relocation subset

- The loader validates a MOD0 header, BSS start/end offsets, and a bounded DT_* table.
- RELA supports only R_AARCH64_NONE and symbol-free R_AARCH64_RELATIVE. RELR supports bounded relative pointer adjustments.
- Symbol-based relocations, PLT/GOT symbol resolution, imported modules, versioning, exception metadata, RELRO permission transitions, and LNY extension behavior are not implemented.


## AArch64 integer pair memory operations

- Integer LDP/STP supports W/X pairs with signed-offset, pre-index, and post-index addressing. SIMD/FP pair loads/stores and non-temporal pair forms remain unsupported.
- Pair accesses use one bounded memory operation; faulting loads leave destination registers and writeback state unchanged. The wider load/store instruction family is still incomplete.


## AArch64 scalar load/store addressing

- Scalar unscaled, pre-index and post-index integer forms are supported for selected byte/halfword/word/doubleword transfers, including sign-extending loads.
- Register-offset addressing, SIMD/FP vector memory operations, unprivileged access, exclusive/atomic operations, and other addressing forms remain unsupported.


## AArch64 register-offset memory operations

- Integer register-offset LDR/STR supports UXTW, LSL, SXTW and SXTX index extensions and optional element-size scaling.
- SIMD/FP vector memory operations and exclusive/atomic accesses remain unsupported; no multi-core memory ordering model is present.


## Initial EL0 SVC dispatch

- SVC 0x10 returns virtual processor 0 because the current execution model has one CPU.
- SVC 0x1E returns the deterministic virtual timer counter. The counter currently advances one tick per guest instruction and is not calibrated to the Switch hardware counter frequency.
- Other SVC numbers are not dispatched and remain on the guest EL1 vector path. WaitSynchronization (0x18), IPC send/receive, thread/process creation and termination, mutex/event semantics, memory mapping/query calls, and the remainder of the Horizon SVC table are not implemented.


## AArch64 literal loads

- PC-relative LDR W, LDR X, and LDRSW literal loads are supported. PRFM literal is treated as a no-op hint.
- SIMD/FP literal loads and full prefetch/cache-hint behavior remain unsupported.


## AArch64 carry arithmetic

- ADC/ADCS and SBC/SBCS are implemented for W/X registers with carry-in and borrow-in semantics and NZCV updates on flag-setting forms.
- This does not make the integer core complete; several extended arithmetic, bitfield, atomic, system, and SIMD instruction groups remain missing.


## AArch64 memory-barrier instruction subset

- DMB and DSB encodings with non-reserved option fields, plus ISB SY, are recognized.
- These instructions retire as sequencing points because the current interpreter executes one guest instruction at a time, memory writes complete synchronously, and instruction fetch is performed afresh for each step.
- This does not implement host-thread fences, a weakly ordered guest memory model, cache maintenance, instruction-cache synchronization, or true SMP ordering. A concurrent CPU/JIT backend must replace this simplified behavior with an explicit architectural memory model.


## SIMD/FP register-pair memory operations

- LDP/STP supports selected S, D, and Q SIMD/FP register pairs with signed-offset, pre-index, and post-index addressing.
- Single-element vector loads/stores, LD1/ST1 structure operations, lane loads/stores, replicate loads, and SIMD/FP register-offset addressing remain unsupported.


## SIMD/FP single-register unsigned-offset loads and stores

- Unsigned-immediate STR/LDR supports S (32-bit), D (64-bit), and Q (128-bit) SIMD/FP register transfers.
- Unscaled, pre-index, post-index, register-offset, structure, lane, and replicate vector memory operations remain unsupported.


## AArch64 acquire/release scalar memory operations

- LDAR and STLR support aligned 32-bit and 64-bit general-register transfers.
- Acquire/release sequencing is represented by synchronous instruction ordering only. There are no host-thread fences or weak-memory visibility rules until a true concurrent guest CPU scheduler is introduced.
