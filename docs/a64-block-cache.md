# AArch64 Basic-Block Cache

The `A64BlockCache` builds short basic blocks of instruction words from executable guest memory. Blocks stop at common control-flow instructions, system-call entry points, or a maximum of 32 instructions. The cached words are executed through the same `A64Interpreter` semantics as the one-instruction runner.

## Cache validity

Each mapped MemoryBus region has a monotonically assigned generation identifier. A write, successful exclusive store, mapping permission change, or replacement mapping advances the identifier. A block records the generation of the region containing its entry point; a stale block is discarded before use. The runner also checks the generation after each instruction so self-modifying code forces a rebuild before the next cached instruction is consumed.

This is deliberately conservative: any write anywhere in the same region invalidates all blocks keyed within that region. It avoids stale instruction reuse without implementing fine-grained page-level invalidation or an instruction cache model.

## Current limits

- The cache runs only against the direct/identity-mapped `MemoryBus` interface; it does not key blocks by translated virtual address plus ASID or MMU context.
- Blocks are cached raw instruction words, not decoded IR or native machine code.
- Execution still calls the A64 interpreter for every instruction. There is no x86-64/ARM64 native emitter, register allocator, constant propagation, speculative specialization, or host code page.
- The A64 interpreter remains incomplete; caching does not add instruction semantics or console compatibility.
- Concurrent guest execution must not call one cache instance from multiple host threads. Each CPU worker should own its own cache or use a future synchronized cache manager.

This feature is a code-cache invalidation and block-execution foundation, not a claim that LucidSwitch2 already has a JIT compiler.
