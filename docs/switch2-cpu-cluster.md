# Switch 2 CPU Cluster Foundation

LucidSwitch2 models an eight-context CPU topology for independent AArch64 execution experiments. This is a generic, testable CPU cluster model; it is not a complete Cortex-A78C implementation.

## Execution modes

- `run_round_robin`: deterministic, one-instruction quanta in round-robin order for reproducible tests.
- `run_parallel`: launches one host worker per core that is runnable when the call begins. Each worker gets a bounded per-core instruction budget. Execution interleaving is nondeterministic.
- Each core has independent general-purpose/SIMD registers, PC, SP, NZCV flags, system-register state, instruction count and exclusive reservation state.
- Guest cores share one checked `MemoryBus`. Mapping and data operations are protected with shared/exclusive host locks. Writes update exclusive-reservation versions atomically with respect to other bus operations.
- A faulting core transitions to `Faulted` without automatically stopping its peers.

## Coherency boundary

The memory bus provides a single coherent shared backing store: a completed bus write is visible to later reads, and a write to a reserved granule invalidates a peer's exclusive-store attempt. `LDXR`/`LDAXR` and `STXR`/`STLXR` remain limited to aligned 32/64-bit operations with a conservative 16-byte reservation-version scheme. `DMB`, `DSB`, `ISB`, `LDAR`, and `STLR` are represented as sequential ordering points in the interpreter.

This is not a cache hierarchy or a full ARM memory model. There are no per-core data/instruction caches, speculative execution, store buffers, cache clean/invalidate operations, TLB shootdowns, or true weak-memory reordering. The bus locks serialize individual memory operations; they do not model real console cache-coherency hardware. The interpreter's acquire/release/barrier semantics are therefore not claimed to be complete under SMP.

## Scope still missing

- Full Cortex-A78C instruction set and architectural feature discovery.
- Native JIT translation and code-cache invalidation.
- Accurate cache topology, cache-maintenance operations and architectural memory ordering.
- Realistic interrupt routing, affinity, timers, power states and Horizon scheduler integration.
- Separate process address spaces and per-core translation state.

Use `--switch2-cpu-demo` to run the deterministic eight-core smoke test. Unit tests exercise both deterministic and parallel worker execution using synthetic instruction streams and shared test memory. No firmware, protected title data, or commercial game files are used.
